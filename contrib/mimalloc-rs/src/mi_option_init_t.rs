use crate::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum mi_option_init_t {
    MI_OPTION_UNINIT,
    MI_OPTION_DEFAULTED,
    MI_OPTION_INITIALIZED,
}

