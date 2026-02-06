use crate::*;
use std::sync::atomic::AtomicU64;
use std::sync::atomic::Ordering;


#[derive(Clone)]
pub struct MiBlock {
    pub next: mi_encoded_t,
}

