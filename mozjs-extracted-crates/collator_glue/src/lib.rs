/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::cmp::Ordering;
use std::collections::HashSet;
use std::ffi::c_char;
use std::sync::OnceLock;

use arrayvec::ArrayVec;
use icu_collator::options::AlternateHandling;
use icu_collator::options::CaseLevel;
use icu_collator::options::Strength;
use icu_collator::preferences::CollationCaseFirst;
use icu_collator::preferences::CollationNumericOrdering;
use icu_collator::CollatorBorrowed;
use icu_collator::CollatorPreferences;
use icu_locale_core::subtags::language;
use icu_locale_core::subtags::script;
use icu_locale_core::subtags::Language;
use icu_locale_core::DataLocale;
use icu_locale_core::Locale;
use tinystr::tinystr;
use tinystr::TinyAsciiStr;
use writeable::Writeable;

/// Currently 10, because the scriptful locales all have two-character
/// language and two-character region, but could grow to 12 with scriptful
/// locales that have three-character language _and_ region.
/// The parameter for `ArrayLocale` below needs to match.
const MAX_LOCALE_LEN: usize = 10;

/// Tiny string for any locale we know about
type TinyLocaleStr = TinyAsciiStr<MAX_LOCALE_LEN>;

/// `Write` sink that can fit locales we know about.
/// Parameter needs to match `MAX_LOCALE_LEN`.
type ArrayLocale = arraystring::ArrayString<arraystring::typenum::U10>;

/// Tiny string holding a collation type. 8 is the maximum length
/// for a BCP-47 subtag. The full length is used by `phonetic`.
type TinyCollationStr = TinyAsciiStr<8>;

/// Locales that are needed on the list for 402 compat
/// or suspected Web compat even if not enumerated by ICU4X.
/// (These should be packed in a fancier way to micro optimize
/// binary size, but not annoying supply-chair-reviewers about
/// the requisite crate for now.)
const ADDITIONAL_LOCALES: [&str; 50] = [
    "de",         // Root is valid.
    "en",         // Root is valid.
    "fr",         // Root is valid.
    "ga",         // Root is valid.
    "id",         // Root is valid.
    "it",         // Root is valid.
    "lb",         // Root is valid.
    "lij",        // Root is valid.
    "ms",         // Root is valid.
    "nl",         // Root is valid.
    "pt",         // Root is valid.
    "st",         // Root is valid.
    "sw",         // Root is valid.
    "xh",         // Root is valid.
    "zu",         // Root is valid.
    "ar-SA",      // Does Web compat really need this?
    "en-GB",      // Does Web compat really need this?
    "en-US",      // Does Web compat really need this?
    "he-IL",      // Does Web compat really need this?
    "id-ID",      // Does Web compat really need this?
    "nb-NO",      // Does Web compat really need this?
    "pa-Guru",    // Is this really needed for resolution, considering that we don't have pa-Arab?
    "pa-Guru-IN", // Is this really needed for resolution, considering that we don't have pa-Arab?
    "pa-IN",      // Is this really needed for resolution, considering that we don't have pa-Arab?
    "sr-BA",      // Does Web compat need this? (Resolution would work without this.)
    "sr-Cyrl",    // Does Web compat need this? (Resolution would work without this.)
    "sr-Cyrl-BA", // Does Web compat need this? (Resolution would work without this.)
    "sr-Cyrl-ME", // Does Web compat need this? (Resolution would work without this.)
    "sr-Cyrl-RS", // Does Web compat need this? (Resolution would work without this.)
    "sr-Latn",    // Needed for resolution to work.
    "sr-Latn-BA", // Does Web compat need this? (Resolution would work without this.)
    "sr-Latn-RS", // Does Web compat need this? (Resolution would work without this.)
    "sr-ME",      // Needed for resolution to work.
    "sr-RS",      // Does Web compat need this? (Resolution would work without this.)
    "zh",    // ICU4X models Chinese collations as `und-Hani`/`und-Hans`/`und-Hant` and not `zh`
    "zh-CN", // Does Web compat need this? (Resolution would work without this.)
    "zh-Hans", // Does Web compat need this? (Resolution would work without this.)
    "zh-Hans-CN", // Does Web compat need this? (Resolution would work without this.)
    "zh-Hans-SG", // Does Web compat need this? (Resolution would work without this.)
    "zh-Hant", // Needed for resolution to work.
    "zh-Hant-HK", // Does Web compat need this? (Resolution would work without this.)
    "zh-Hant-MO", // Does Web compat need this? (Resolution would work without this.)
    "zh-Hant-TW", // Does Web compat need this? (Resolution would work without this.)
    "zh-HK", // Needed for resolution to work.
    "zh-MO", // Needed for resolution to work.
    "zh-SG", // Does Web compat need this? (Resolution would work without this.)
    "zh-TW", // Needed for resolution to work.
    "nn",    // Collates like `no`; special case that's supported but not enumerated by ICU4X.
    "nb",    // Collates like `no`; special case that's supported but not enumerated by ICU4X.
    "ff", // Satisfy algorithm requirements, see https://unicode-org.atlassian.net/browse/CLDR-19271
];

/// Pair of a language and a collation that is valid for it.
#[derive(Eq, PartialEq, Hash)]
pub(crate) struct LangColl {
    pub(crate) lang: Language,
    pub(crate) coll: TinyCollationStr,
}

impl LangColl {
    /// Constructor
    pub(crate) fn new(lang: Language, coll: TinyCollationStr) -> Self {
        Self { lang, coll }
    }
}

/// If this overflows after an ICU4X update, simply adjust the number.
const LANG_COLL_COMBINATIONS_SIZE: usize = 18;

/// For checking if a given language supports a given collation identifier.
static LANG_COLL_COMBINATIONS: OnceLock<ArrayVec<LangColl, LANG_COLL_COMBINATIONS_SIZE>> =
    OnceLock::new();

/// Which differences in the strings should lead to differences in collation
/// comparisons.
///
/// ECMA-402 semantics apply: https://tc39.es/ecma402/#sec-collator-comparestrings
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum CollatorSensitivity {
    /// Strings that differ in base letters, accents and other diacritic marks,
    /// or case compare as unequal. Other differences may also be taken into
    /// consideration.
    /// Examples: a ≠ b, a ≠ á, a ≠ A.
    Variant = 0,
    /// Only strings that differ in base letters or case compare as unequal.
    /// Examples: a ≠ b, a = á, a ≠ A.
    Case = 1,
    /// Only strings that differ in base letters or accents and other diacritic
    /// marks compare as unequal.
    /// Examples: a ≠ b, a ≠ á, a = A.
    Accent = 2,
    /// Only strings that differ in base letters compare as unequal.
    /// Examples: a ≠ b, a = á, a = A.
    Base = 3,
}

impl Default for CollatorSensitivity {
    fn default() -> Self {
        Self::Variant
    }
}

/// Whether to give case special precedence.
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum CollatorCaseFirst {
    /// Use the default value for locale. (In practice `Upper` for
    /// Danish and Maltese and `False` for other languages. Can also
    /// occur as an explicit value in the locale identifier.)
    Locale = 0,
    /// Sort upper case first.
    Upper = 1,
    /// Sort lower case first.
    Lower = 2,
    /// Orders upper and lower case letters in accordance to their tertiary
    /// weights.
    False = 3,
}

impl Default for CollatorCaseFirst {
    fn default() -> Self {
        Self::Locale
    }
}

/// Whether to ignore punctuation.
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum CollatorIgnorePunctuation {
    /// Use the default value for locale. (In practice `On` for Thai
    /// and `Off` for other languages.)
    Locale = 0,
    /// Ignore punctuation.
    On = 1,
    /// Don't ignore punctuation.
    Off = 2,
}

impl Default for CollatorIgnorePunctuation {
    fn default() -> Self {
        Self::Locale
    }
}

/// Whether to sort sequences of decimal digits according to the
/// numeric value of the sequence or on a per-character basis.
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum CollatorNumeric {
    /// Use the value from the extension tag in the locale identifier
    /// or `Off` otherwise.
    Locale = 0,
    /// Sort by numeric interpretation of decimal digit sequence.
    On = 1,
    /// Sort by digits as individual characters.
    Off = 2,
}

impl Default for CollatorNumeric {
    fn default() -> Self {
        Self::Locale
    }
}

/// FFI-friendly version of the option bag.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct CollatorOptions {
    pub sensitivity: CollatorSensitivity,
    pub case_first: CollatorCaseFirst,
    pub ignore_punctuation: CollatorIgnorePunctuation,
    pub numeric: CollatorNumeric,
}

/// Constructor for the collator.
///
/// Returns a null pointer if `locale` and `locale_len`
/// don't specify a locale id that parses successfully.
///
/// If the locale parses successfully, a non-null collator
/// is returned even if the locale is unknown.
///
/// Non-null return value must be freed with
/// `mozilla_collator_glue_collator_free`.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collator_try_new(
    locale: *const c_char,
    locale_len: usize,
    options: crate::CollatorOptions,
) -> *mut CollatorBorrowed<'static> {
    if locale_len == 0 {
        return core::ptr::null_mut();
    }
    let mut prefs: CollatorPreferences = if let Ok(locale) =
        Locale::try_from_utf8(core::slice::from_raw_parts(locale as *const u8, locale_len))
    {
        locale.into()
    } else {
        return core::ptr::null_mut();
    };
    let mut collator_options = icu_collator::options::CollatorOptions::default();
    match options.sensitivity {
        CollatorSensitivity::Base => {
            collator_options.strength = Some(Strength::Primary);
        }
        CollatorSensitivity::Accent => {
            collator_options.strength = Some(Strength::Secondary);
        }
        CollatorSensitivity::Case => {
            collator_options.strength = Some(Strength::Primary);
            collator_options.case_level = Some(CaseLevel::On);
        }
        CollatorSensitivity::Variant => {
            collator_options.strength = Some(Strength::Tertiary);
        }
    }
    match options.case_first {
        CollatorCaseFirst::Locale => {}
        CollatorCaseFirst::Upper => prefs.case_first = Some(CollationCaseFirst::Upper),
        CollatorCaseFirst::Lower => prefs.case_first = Some(CollationCaseFirst::Lower),
        CollatorCaseFirst::False => prefs.case_first = Some(CollationCaseFirst::False),
    }
    match options.ignore_punctuation {
        CollatorIgnorePunctuation::Locale => {}
        CollatorIgnorePunctuation::On => {
            collator_options.alternate_handling = Some(AlternateHandling::Shifted);
        }
        CollatorIgnorePunctuation::Off => {
            collator_options.alternate_handling = Some(AlternateHandling::NonIgnorable);
        }
    }
    match options.numeric {
        CollatorNumeric::Locale => {}
        CollatorNumeric::On => {
            prefs.numeric_ordering = Some(CollationNumericOrdering::True);
        }
        CollatorNumeric::Off => {
            prefs.numeric_ordering = Some(CollationNumericOrdering::False);
        }
    }
    // `unwrap` is OK below, because `CollatorBorrowed::try_new`` never
    // fails with properly-generated baked data.
    // See https://github.com/unicode-org/icu4x/issues/6634
    Box::into_raw(Box::new(
        CollatorBorrowed::try_new(prefs, collator_options).unwrap(),
    ))
}

/// Deleter for values previously obtained from
/// `mozilla_collator_glue_collator_try_new`.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collator_free(
    collator: *mut CollatorBorrowed<'static>,
) {
    let _ = Box::from_raw(collator);
}

/// Returns the resolved options of the collator.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collator_resolved_options(
    collator: *const CollatorBorrowed<'static>,
) -> CollatorOptions {
    let resolved = (*collator).resolved_options();

    CollatorOptions {
        sensitivity: match resolved.strength {
            Strength::Primary => match resolved.case_level {
                CaseLevel::Off => CollatorSensitivity::Base,
                CaseLevel::On => CollatorSensitivity::Case,
                _ => panic!("unexpected case level: unknown"),
            },
            Strength::Secondary => CollatorSensitivity::Accent,
            Strength::Tertiary => CollatorSensitivity::Variant,
            Strength::Quaternary => panic!("unexpected strength: quaternary"),
            Strength::Identical => panic!("unexpected strength: identical"),
            _ => panic!("unexpected strength: unknown"),
        },

        case_first: match resolved.case_first {
            CollationCaseFirst::Upper => CollatorCaseFirst::Upper,
            CollationCaseFirst::Lower => CollatorCaseFirst::Lower,
            CollationCaseFirst::False => CollatorCaseFirst::False,
            _ => panic!("unexpected case first: unknown"),
        },

        ignore_punctuation: match resolved.alternate_handling {
            AlternateHandling::Shifted => CollatorIgnorePunctuation::On,
            AlternateHandling::NonIgnorable => CollatorIgnorePunctuation::Off,
            _ => panic!("unexpected alternate handling: unknown"),
        },

        numeric: match resolved.numeric {
            CollationNumericOrdering::True => CollatorNumeric::On,
            CollationNumericOrdering::False => CollatorNumeric::Off,
            _ => panic!("unexpected numeric ordering: unknown"),
        },
    }
}

/// Compares UTF-16 strings.
///
/// Unpaired surrogates are treated as the REPLACEMENT CHARACTER.
///
/// # Safety
///
/// Note that we're relying on mozilla::Span to provide a non-null
/// pointer for empty spans, which works for now, but probably should
/// be changed on the mozilla::Span side eventually.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collator_compare_utf16(
    collator: *const CollatorBorrowed<'static>,
    left: *const u16,
    left_len: usize,
    right: *const u16,
    right_len: usize,
) -> i32 {
    (*collator).compare_utf16(
        core::slice::from_raw_parts(left, left_len),
        core::slice::from_raw_parts(right, right_len),
    ) as i32
}

/// Compares Latin1 strings.
///
/// # Safety
///
/// Note that we're relying on mozilla::Span to provide a non-null
/// pointer for empty spans, which works for now, but probably should
/// be changed on the mozilla::Span side eventually.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collator_compare_latin1(
    collator: *const CollatorBorrowed<'static>,
    left: *const u8,
    left_len: usize,
    right: *const u8,
    right_len: usize,
) -> i32 {
    (*collator).compare_latin1(
        core::slice::from_raw_parts(left, left_len),
        core::slice::from_raw_parts(right, right_len),
    ) as i32
}

/// Compares Latin1 string to a UTF-16 string.
///
/// # Safety
///
/// Note that we're relying on mozilla::Span to provide a non-null
/// pointer for empty spans, which works for now, but probably should
/// be changed on the mozilla::Span side eventually.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collator_compare_latin1_utf16(
    collator: *const CollatorBorrowed<'static>,
    left: *const u8,
    left_len: usize,
    right: *const u16,
    right_len: usize,
) -> i32 {
    (*collator).compare_latin1_utf16(
        core::slice::from_raw_parts(left, left_len),
        core::slice::from_raw_parts(right, right_len),
    ) as i32
}

/// Checks if a given collation is supported for a given language.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_is_supported_collation(
    locale: *const u8,
    locale_len: usize,
    collation: *const u8,
    collation_len: usize,
) -> bool {
    if locale_len == 0 || collation_len == 0 {
        return false;
    }
    is_supported_collation(
        core::slice::from_raw_parts(locale, locale_len),
        core::slice::from_raw_parts(collation, collation_len),
    )
}

fn get_lang_coll_combinations() -> &'static [LangColl] {
    LANG_COLL_COMBINATIONS.get_or_init(|| {
        let mut set: HashSet<LangColl> = HashSet::new();
        #[cfg(debug_assertions)]
        let mut eor_seen = false;
        #[cfg(debug_assertions)]
        let mut emoji_seen = false;
        for (loc, collation) in icu_collator::provider::list_locales() {
            if collation.is_empty() || collation.as_str() == "search" {
                continue;
            }
            if loc.language == language!("und") {
                if let Some(script) = loc.script {
                    if script == script!("Hani") {
                        // 402 expects `zh` but ICU4X enumerates `und-Hani`.
                        set.insert(LangColl::new(language!("zh"), collation));
                    } else {
                        debug_assert!(
                            script == script!("Hant") || script == script!("Hans"),
                            "Need to update this code to accommodate an ICU4X change!"
                        );
                    }
                } else {
                    #[cfg(debug_assertions)]
                    {
                        if collation.as_str() == "eor" {
                            eor_seen = true;
                        } else if collation.as_str() == "emoji" {
                            emoji_seen = true;
                        } else {
                            debug_assert!(
                                false,
                                "Need to update this code to accommodate an ICU4X change!"
                            );
                        }
                    }
                }
            } else {
                set.insert(LangColl::new(loc.language, collation));
            }
        }
        #[cfg(debug_assertions)]
        {
            debug_assert!(eor_seen, "ICU4X data should be generated with eor enabled");
            debug_assert!(
                emoji_seen,
                "ICU4X data should be generated with emoji enabled"
            );
        }
        let mut arr: ArrayVec<LangColl, LANG_COLL_COMBINATIONS_SIZE> = set.drain().collect();
        // Reverse-sort to put the case that makes the most sense to specify explicitly,
        // zh-u-co-zhuyin, early for linear search.
        arr.sort_by(|a, b| {
            let lang_cmp = b.lang.cmp(&a.lang);
            if lang_cmp == Ordering::Equal {
                b.coll.cmp(&a.coll)
            } else {
                lang_cmp
            }
        });
        arr
    })
}

/// Checks if a given collation is supported for a given language.
fn is_supported_collation(locale: &[u8], collation: &[u8]) -> bool {
    let Ok(locale) = Locale::try_from_utf8(locale) else {
        return false;
    };
    // We could be fancy and actually check if `eor` and `emoji`
    // data was generated, but it's supposed to be generated,
    // and tests will catch the actual behavior.
    if collation == b"eor" || collation == b"emoji" {
        return true;
    }
    // Currently all collation identifiers attach to language without
    // region or script qualifiers.
    let lang = locale.id.language;
    let Ok(coll) = TinyCollationStr::try_from_utf8(collation) else {
        return false;
    };
    let langcoll = LangColl::new(lang, coll);
    get_lang_coll_combinations()
        .iter()
        .any(|combination| combination == &langcoll)
}

/// Data holder for enumerating supported locales.
pub struct LocaleList {
    vec: Vec<TinyLocaleStr>,
}

/// Converts from ICU4X `DataLocale` to `TinyLocaleStr`.
fn data_locale_to_tiny(loc: DataLocale) -> TinyLocaleStr {
    let mut buf: ArrayLocale = ArrayLocale::new();
    loc.write_to(&mut buf).expect("Locale fits in max length");
    TinyAsciiStr::<MAX_LOCALE_LEN>::try_from_str(&buf)
        .expect("Locale still fits in max length and is ASCII")
}

/// List the supported locales by merging locales enumerated by ICU4X and
/// additional locales that ICU4X doesn't enumerate but SpiderMonkey needs
/// to see enumerated.
fn list_locales() -> LocaleList {
    let mut locales: HashSet<TinyLocaleStr> = HashSet::new();
    for loc in ADDITIONAL_LOCALES {
        let _ = locales.insert(
            TinyLocaleStr::try_from_str(loc).expect("additional list should have valid locales"),
        );
    }
    for (loc, _) in icu_collator::provider::list_locales() {
        // Root and Chinese modeled as `und` and filled in above instead.
        if loc.language == language!("und") {
            continue;
        }
        let _ = locales.insert(data_locale_to_tiny(loc));
    }
    LocaleList {
        vec: locales.iter().copied().collect(),
    }
}

/// List supported locales for FFI.
/// The return value must be freed using `mozilla_collator_glue_locale_list_free`.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_locale_list_new() -> *mut LocaleList {
    Box::into_raw(Box::new(list_locales()))
}

/// Read an item from the locale list.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_locale_list_item(
    list: *mut LocaleList,
    index: usize,
    len: *mut usize,
) -> *const c_char {
    let s = (&(*list).vec)[index].as_str();
    *len = s.len();
    s.as_ptr() as *const c_char
}

/// Get the length of the locale list.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_locale_list_len(list: *mut LocaleList) -> usize {
    (&(*list).vec).len()
}

/// Free the locale list returned by `mozilla_collator_glue_locale_list_new`.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_locale_list_free(list: *mut LocaleList) {
    let _ = Box::from_raw(list);
}

/// Holder for supported collation identifiers.
pub struct CollationList {
    vec: Vec<TinyCollationStr>,
}

/// List the supported collation identifiers excluding `standard` and `search`.
fn list_collations() -> CollationList {
    let mut collations: HashSet<TinyCollationStr> = HashSet::new();

    // We could be fancy and actually check if `eor` and `emoji` data was
    // generated, but it's supposed to be generated, and tests will catch
    // the actual behavior.
    let _ = collations.insert(tinystr!(8, "emoji"));
    let _ = collations.insert(tinystr!(8, "eor"));

    for langcoll in get_lang_coll_combinations() {
        let _ = collations.insert(langcoll.coll);
    }
    CollationList {
        vec: collations.iter().copied().collect(),
    }
}

/// List the supported collation identifiers for FFI.
/// The return value must be freed with `mozilla_collator_glue_collation_list_free`.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collation_list_new() -> *mut CollationList {
    Box::into_raw(Box::new(list_collations()))
}

/// Read an items from the collation identifier list.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collation_list_item(
    list: *mut CollationList,
    index: usize,
    len: *mut usize,
) -> *const c_char {
    let s = (&(*list).vec)[index].as_str();
    *len = s.len();
    s.as_ptr() as *const c_char
}

/// Get the length of the collation identifier list.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collation_list_len(
    list: *mut CollationList,
) -> usize {
    (&(*list).vec).len()
}

/// Free the collation identifier list received from `mozilla_collator_glue_collation_list_new`.
#[no_mangle]
pub unsafe extern "C" fn mozilla_collator_glue_collation_list_free(list: *mut CollationList) {
    let _ = Box::from_raw(list);
}
