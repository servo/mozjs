use mozjs::context::*;
use std::marker::PhantomData;

fn can_cause_gc(cx: &mut JSContext) -> bool {
    actually_causes_gc(cx) && can_cause_gc2(cx)
}

fn can_cause_gc2(cx: &mut JSContext) -> bool {
    actually_causes_gc(cx);
    {
        let no_gc = cx.no_gc();
    }
    actually_causes_gc(cx)
}

fn actually_causes_gc(_cx: &mut JSContext) -> bool {
    true
}

struct ShouldNotBeHoldAcrossGC<'a>(PhantomData<&'a ()>);

fn something_that_cannot_hold_across_gc<'a>(_no_gc: &NoGC<'a>) -> ShouldNotBeHoldAcrossGC<'a> {
    ShouldNotBeHoldAcrossGC(PhantomData)
}

fn main() {
    let mut cx = unsafe { JSContext::from_ptr(std::ptr::null_mut()) };
    can_cause_gc(&mut cx);
    let block_gc = something_that_cannot_hold_across_gc(cx.no_gc());
    drop(block_gc);
    can_cause_gc(&mut cx);
}
