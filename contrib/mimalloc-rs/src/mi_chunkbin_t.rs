use crate::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MiChunkbinE {
    MI_CBIN_SMALL,
    MI_CBIN_OTHER,
    MI_CBIN_MEDIUM,
    MI_CBIN_LARGE,
    MI_CBIN_NONE,
    MI_CBIN_COUNT,
}

pub type MiChunkbinT = MiChunkbinE;

