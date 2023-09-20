mod generated {
    pub(super) mod gluebindings;
}

use core::mem;

pub use generated::gluebindings::root::*;

pub type EncodedStringCallback = fn(*const core::ffi::c_char);

// manual glue stuff
unsafe impl Sync for ProxyTraps {}

impl Default for JobQueueTraps {
    fn default() -> JobQueueTraps {
        unsafe { mem::zeroed() }
    }
}

impl Default for ProxyTraps {
    fn default() -> ProxyTraps {
        unsafe { mem::zeroed() }
    }
}

impl Default for WrapperProxyHandler {
    fn default() -> WrapperProxyHandler {
        unsafe { mem::zeroed() }
    }
}

impl Default for ForwardingProxyHandler {
    fn default() -> ForwardingProxyHandler {
        unsafe { mem::zeroed() }
    }
}
