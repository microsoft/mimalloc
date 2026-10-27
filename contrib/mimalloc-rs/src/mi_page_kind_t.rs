use crate::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MiPageKind {
    MI_PAGE_SMALL,
    MI_PAGE_MEDIUM,
    MI_PAGE_LARGE,
    MI_PAGE_SINGLETON,
}

