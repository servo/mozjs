/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


"use strict";

loadRelativeToScript('callgraph.js');

var options = parse_options([
    {
        name: '--verbose',
        type: 'bool'
    },
    {
        name: '--function',
        type: 'string'
    },
    {
        name: 'typeInfo_filename',
        type: 'string',
        default: "typeInfo.txt"
    },
    {
        name: 'callgraphOut_filename',
        type: 'string',
        default: "rawcalls.txt"
    },
    {
        name: 'batch',
        default: 1,
        type: 'number'
    },
    {
        name: 'numBatches',
        default: 1,
        type: 'number'
    },
]);

var origOut = os.file.redirect(options.callgraphOut_filename);
var typeInfo = loadTypeInfo(options.typeInfo_filename);

var memoized = new Map();

var unmangled2id = new Set();

// Insert a string into the name table and return the ID. Do not use for
// functions, which must be handled specially.
function getId(name)
{
    let id = memoized.get(name);
    if (id !== undefined)
        return id;

    id = memoized.size + 1;
    memoized.set(name, id);
    print(`#${id} ${name}`);

    return id;
}

// Split a function into mangled and unmangled parts and return the ID for the
// function.
function functionId(name)
{
    const [mangled, unmangled] = splitFunction(name);
    const id = getId(mangled);

    // Only produce a mangled -> unmangled mapping once, unless there are
    // multiple unmangled names for the same mangled name.
    if (unmangled2id.has(unmangled))
        return id;

    print(`= ${id} ${unmangled}`);
    unmangled2id.add(unmangled);
    return id;
}

var lastline;
function printOnce(line)
{
    if (line != lastline) {
        print(line);
        lastline = line;
    }
}

// Returns a table mapping function name to lists of
// [annotation-name, annotation-value] pairs:
//   { function-name => [ [annotation-name, annotation-value] ] }
//
// Note that sixgill will only store certain attributes (annotation-names), so
// this won't be *all* the attributes in the source, just the ones that sixgill
// watches for.
function getAllAttributes(ffg)
{
    var all_annotations = {};
    ffg.forEachDecl(decl => {
        if (decl.Variable.Kind == 'Func') {
            const name = decl.Variable.Name[0];
            const annotations = all_annotations[name] = [];

            for (var ann of (decl.Type.Annotation || [])) {
                annotations.push(ann.Name);
            }
        }
    });

    return all_annotations;
}

// Get just the annotations understood by the hazard analysis.
function getAnnotations(ffg) {
    var tags = new Set();
    var attributes = getAllAttributes(ffg);
    if (ffg.name in attributes) {
        for (var [ annName, annValue ] of attributes[ffg.name]) {
            if (annName == 'annotate')
                tags.add(annValue);
        }
    }
    return tags;
}

// Scan through a function body, pulling out all annotations and calls and
// recording them in callgraph.txt.
function processBody(ffg, body) {
    if (!('PEdge' in body))
        return;

    for (var tag of getAnnotations(ffg).values()) {
        const id = functionId(ffg.name);
        print(`T ${id} ${tag}`);
        if (tag == "Calls JSNatives")
            printOnce(`D ${id} ${functionId("(js-code)")}`);
    }

    // Set of all callees that have been output so far, in order to suppress
    // repeated callgraph edges from being recorded. This uses a Map from
    // callees to limit sets, because we don't want a limited edge to prevent
    // an unlimited edge from being recorded later. (So an edge will be skipped
    // if it exists and is at least as limited as the previously seen edge.)
    //
    // Limit sets are implemented as integers interpreted as bitfields.
    //
    var seen = new Map();

    lastline = null;
    for (var edge of body.PEdge) {
        if (edge.Kind != "Call")
            continue;

        // The attrs (eg ATTR_GC_SUPPRESSED) are determined by whatever RAII
        // scopes might be active, which have been computed previously for all
        // points in the body.
        const scopeAttrs = body.attrs[edge.Index[0]] | 0;

        for (const { callee, attrs } of getCallees(ffg, body, edge, scopeAttrs)) {
            // Some function names will be synthesized by manually constructing
            // their names. Verify that we managed to synthesize an existing function.
            // This cannot be done later with either the callees or callers tables,
            // because the function may be an otherwise uncalled leaf.
            if (attrs & ATTR_SYNTHETIC) {
                assertFunctionExists(callee.name);
            }

            // Individual callees may have additional attrs. The only such
            // bit currently is that nsISupports.{AddRef,Release} are assumed
            // to never GC.
            let prologue = attrs ? `/${attrs} ` : "";
            prologue += functionId(ffg.name) + " ";
            if (callee.kind == 'direct') {
                const prev_attrs = seen.has(callee.name) ? seen.get(callee.name) : ATTRS_UNVISITED;
                if (prev_attrs & ~attrs) {
                    // Only output an edge if it loosens a limit.
                    seen.set(callee.name, prev_attrs & attrs);
                    printOnce("D " + prologue + functionId(callee.name));
                }
            } else if (callee.kind == 'field') {
                var { csu, field, isVirtual } = callee;
                const tag = isVirtual ? 'V' : 'F';
                const fullfield = `${csu}.${field}`;
                printOnce(`${tag} ${prologue}${getId(fullfield)} CLASS ${csu} FIELD ${field}`);
            } else if (callee.kind == 'resolved-field') {
                // Fully-resolved field (virtual method) call. Record the
                // callgraph edges. Do not consider attrs, since they are local
                // to this callsite and we are writing out a global record
                // here.
                //
                // Any field call that does *not* have an R entry must be
                // assumed to call anything.
                var { csu, field, callees } = callee;
                var fullFieldName = csu + "." + field;
                if (!virtualResolutionsSeen.has(fullFieldName)) {
                    virtualResolutionsSeen.add(fullFieldName);
                    for (var target of callees)
                        printOnce("R " + getId(fullFieldName) + " " + functionId(target.name));
                }
            } else if (callee.kind == 'indirect') {
                printOnce("I " + prologue + "VARIABLE " + callee.variable);
            } else if (callee.kind == 'unknown') {
                printOnce("I " + prologue + "VARIABLE UNKNOWN");
            } else {
                printErr("invalid " + callee.kind + " callee");
                debugger;
            }
        }
    }
}

// Reserve IDs for special function names.

// represents anything that can run JS
assert(ID.jscode == functionId("(js-code)"));

// function pointers will get an edge to this in loadCallgraph.js; only the ID
// reservation is present in callgraph.txt
assert(ID.anyfunc == functionId("(any-function)"));

// same as above, but for fields annotated to never GC
assert(ID.nogcfunc == functionId("(nogc-function)"));

// garbage collection
assert(ID.gc == functionId("(GC)"));

loadTypes("src_comp.xdb");

// Arbitrary JS code must always be assumed to GC. In real code, there would
// always be a path anyway through some arbitrary JSNative, but this route will be shorter.
print(`D ${ID.jscode} ${ID.gc}`);

// An unknown function is assumed to GC.
print(`D ${ID.anyfunc} ${ID.gc}`);

// Output call edges for all virtual methods defined anywhere, from
// Class.methodname to what a (dynamic) instance of Class would run when
// methodname was called (either Class::methodname() if defined, or some
// Base::methodname() for inherited method definitions).
for (const [fieldkey, methods] of virtualDefinitions) {
    const caller = getId(fieldkey);
    for (const name of methods) {
        const callee = functionId(name);
        printOnce(`D ${caller} ${callee}`);
    }
}

// Output call edges from C.methodname -> S.methodname for all subclasses S of
// class C. This is for when you are calling methodname on a pointer/ref of
// dynamic type C, so that the callgraph contains calls to all descendant
// subclasses' implementations.
for (const [csu, methods] of virtualDeclarations) {
    for (const {field, dtor} of methods) {
        const caller = getId(fieldKey(csu, field));
        if (virtualCanRunJS(typeInfo, csu, field.Name[0]))
            printOnce(`D ${caller} ${functionId("(js-code)")}`);
        if (dtor)
            printOnce(`D ${caller} ${functionId(dtor)}`);
        if (!subclasses.has(csu))
            continue;
        for (const sub of subclasses.get(csu)) {
            printOnce(`D ${caller} ${getId(fieldKey(sub, field))}`);
        }
    }
}

var xdb = xdbLibrary();
xdb.open("src_body.xdb");

if (options.verbose) {
    printErr("Finished loading data structures");
}

var minStream = xdb.min_data_stream();
var maxStream = xdb.max_data_stream();

if (options.function) {
    var index = xdb.lookup_key(options.function);
    if (!index) {
        printErr("Function not found");
        quit(1);
    }
    minStream = maxStream = index;
}

function assertFunctionExists(name) {
    var data = xdb.read_entry(name);
    assert(data.contents != 0, `synthetic function '${name}' not found!`);
}

function process(ffg) {
    ffg.forEachBody(body => { body.attrs = []; });

    for (const body of ffg.bodies) {
        for (var [pbody, id, attrs] of allRAIIGuardedCallPoints(ffg, body)) {
            pbody.attrs[id] = attrs;
        }
    }

    if (options.function) {
        debugger;
    }
    ffg.forEachBody(body => processBody(ffg, body));

    // Not strictly necessary, but add an edge from the synthetic "(js-code)"
    // to RunScript to allow better stacks than just randomly selecting a
    // JSNative to blame things on.
    if (ffg.name.includes("js::RunScript"))
        print(`D ${functionId("(js-code)")} ${functionId(ffg.name)}`);

    // GCC generates multiple constructors and destructors ("in-charge" and
    // "not-in-charge") to handle virtual base classes. They are normally
    // identical, and it appears that GCC does some magic to alias them to the
    // same thing. But this aliasing is not visible to the analysis. So we'll
    // add a dummy call edge from "foo" -> "foo *INTERNAL* ", since only "foo"
    // will show up as called but only "foo *INTERNAL* " will be emitted in the
    // case where the constructors are identical.
    //
    // This is slightly conservative in the case where they are *not*
    // identical, but that should be rare enough that we don't care.
    var markerPos = ffg.name.indexOf(internalMarker);
    if (markerPos > 0) {
        var inChargeXTor = ffg.name.replace(internalMarker, "");
        printOnce("D " + functionId(inChargeXTor) + " " + functionId(ffg.name));
    }

    const [ mangled, unmangled ] = splitFunction(ffg.name);

    // Further note: from https://itanium-cxx-abi.github.io/cxx-abi/abi.html the
    // different kinds of constructors/destructors are:
    // C1	# complete object constructor
    // C2	# base object constructor
    // C3	# complete object allocating constructor
    // D0	# deleting destructor
    // D1	# complete object destructor
    // D2	# base object destructor
    //
    // In actual practice, I have observed C4 and D4 xtors generated by gcc
    // 4.9.3 (but not 4.7.3). The gcc source code says:
    //
    //   /* This is the old-style "[unified]" constructor.
    //      In some cases, we may emit this function and call
    //      it from the clones in order to share code and save space.  */
    //
    // Unfortunately, that "call... from the clones" does not seem to appear in
    // the CFG we get from GCC. So if we see a C4 constructor or D4 destructor,
    // inject an edge to it from C1, C2, and C3 (or D1, D2, and D3). (Note that
    // C3 isn't even used in current GCC, but add the edge anyway just in
    // case.)
    //
    // from gcc/cp/mangle.c:
    //
    // <special-name> ::= D0 # deleting (in-charge) destructor
    //                ::= D1 # complete object (in-charge) destructor
    //                ::= D2 # base object (not-in-charge) destructor
    // <special-name> ::= C1   # complete object constructor
    //                ::= C2   # base object constructor
    //                ::= C3   # complete object allocating constructor
    //
    // Currently, allocating constructors are never used.
    //
    if (ffg.name.indexOf("C4") != -1) {
        // E terminates the method name (and precedes the method parameters).
        // If eg "C4E" shows up in the mangled name for another reason, this
        // will create bogus edges in the callgraph. But it will affect little
        // and is somewhat difficult to avoid, so we will live with it.
        //
        // Another possibility! A templatized constructor will contain C4I...E
        // for template arguments.
        //
        for (let [synthetic, variant, desc] of [
            ['C4E', 'C1E', 'complete_ctor'],
            ['C4E', 'C2E', 'base_ctor'],
            ['C4E', 'C3E', 'complete_alloc_ctor'],
            ['C4I', 'C1I', 'complete_ctor'],
            ['C4I', 'C2I', 'base_ctor'],
            ['C4I', 'C3I', 'complete_alloc_ctor']])
        {
            if (mangled.indexOf(synthetic) == -1)
                continue;

            let variant_mangled = mangled.replace(synthetic, variant);
            let variant_full = `${variant_mangled}$${unmangled} [[${desc}]]`;
            printOnce("D " + functionId(variant_full) + " " + functionId(ffg.name));
        }
    }

    // For destructors:
    //
    // I've never seen D4Ev() + D4Ev(int32), only one or the other. So
    // for a D4Ev of any sort, create:
    //
    //   D0() -> D1()  # deleting destructor calls complete destructor, then deletes
    //   D1() -> D2()  # complete destructor calls base destructor, then destroys virtual bases
    //   D2() -> D4(?) # base destructor might be aliased to unified destructor
    //                 # use whichever one is defined, in-charge or not.
    //                 # ('?') means either () or (int32).
    //
    // Note that this doesn't actually make sense -- D0 and D1 should be
    // in-charge, but gcc doesn't seem to give them the in-charge parameter?!
    //
    if (ffg.name.indexOf("D4Ev") != -1 && ffg.name.indexOf("::~") != -1) {
        const not_in_charge_dtor = ffg.name.replace("(int32)", "()");
        const D0 = not_in_charge_dtor.replace("D4Ev", "D0Ev") + " [[deleting_dtor]]";
        const D1 = not_in_charge_dtor.replace("D4Ev", "D1Ev") + " [[complete_dtor]]";
        const D2 = not_in_charge_dtor.replace("D4Ev", "D2Ev") + " [[base_dtor]]";
        printOnce("D " + functionId(D0) + " " + functionId(D1));
        printOnce("D " + functionId(D1) + " " + functionId(D2));
        printOnce("D " + functionId(D2) + " " + functionId(ffg.name));
    }

    if (isJSNative(mangled))
        printOnce(`D ${functionId("(js-code)")} ${functionId(ffg.name)}`);
}

var start = batchStart(options.batch, options.numBatches, minStream, maxStream);
var end = batchLast(options.batch, options.numBatches, minStream, maxStream);

for (var nameIndex = start; nameIndex <= end; nameIndex++) {
    var name = xdb.read_key(nameIndex);
    var data = xdb.read_entry(name);
    const ffg = new FunctionFlowGraph({ name: name.readString(), bodies: JSON.parse(data.readString()), typeInfo });
    process(ffg);
    xdb.free_string(name);
    xdb.free_string(data);
}

os.file.close(os.file.redirect(origOut));
