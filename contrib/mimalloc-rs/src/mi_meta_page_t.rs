use crate::*;

pub struct mi_meta_page_t {
    pub next: std::sync::atomic::AtomicPtr<mi_meta_page_t>,
    pub memid: MiMemid,
    pub blocks_free: crate::mi_bbitmap_t::mi_bbitmap_t,
}

