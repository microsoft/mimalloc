use crate::*;
use std::sync::atomic::AtomicPtr;
use std::sync::atomic::Ordering;


#[derive(Clone)]
pub struct MiPage;

pub type mi_submap_t = Option<Box<Vec<Option<Box<MiPage>>>>>;

