#[macro_export]
macro_rules! rooted {
	(in($cx:expr) let $($var:ident)+ = $init:expr) => {
        let mut __root = ::std::mem::MaybeUninit::uninit();
        let $($var)+ = $crate::gc::RootedGuard::new($cx, &mut __root, $init);
    };
	(in($cx:expr) let $($var:ident)+: $type:ty = $init:expr) => {
        let mut __root = ::std::mem::MaybeUninit::uninit();
        let $($var)+: $crate::gc::RootedGuard<$type> = $crate::gc::RootedGuard::new($cx, &mut __root, $init);
    };
	(in($cx:expr) let $($var:ident)+: $type:ty) => {
        let mut __root = ::std::mem::MaybeUninit::uninit();
        // SAFETY:
        // We're immediately storing the initial value in a rooted location.
        let $($var)+: $crate::gc::RootedGuard<$type> = $crate::gc::RootedGuard::new(
            $cx,
            &mut __root,
            unsafe { <$type as $crate::gc::GCMethods>::initial() },
        );
    };
}

#[macro_export]
macro_rules! rooted_vec {
    (let mut $name:ident) => {
        let mut __root = $crate::gc::RootableVec::new_unrooted();
        let mut $name = $crate::gc::RootedVec::new(&mut __root);
    };
    (let $name:ident <- $iter:expr) => {
        let mut __root = $crate::gc::RootableVec::new_unrooted();
        let $name = $crate::gc::RootedVec::from_iter(&mut __root, $iter);
    };
    (let mut $name:ident <- $iter:expr) => {
        let mut __root = $crate::gc::RootableVec::new_unrooted();
        let mut $name = $crate::gc::RootedVec::from_iter(&mut __root, $iter);
    };
}

#[macro_export]
macro_rules! auto_root {
    (in($cx:expr) let $($var:ident)+ = $init:expr) => {
        let mut __root = $crate::gc::CustomAutoRooter::new($init);
        let $($var)+ = __root.root($cx);
    };
	(in($cx:expr) let $($var:ident)+: $type:ty = $init:expr) => {
        let mut __root = $crate::gc::CustomAutoRooter::new($init);
        let $($var)+: $crate::rust::CustomAutoRootedGuard<$type> = __root.root($cx);
    };
}
