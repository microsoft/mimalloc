use crate::*;

#[derive(Clone)]
pub struct mi_option_desc_t {
    pub value: isize,
    pub init: crate::mi_option_init_t::mi_option_init_t,
    pub option: MiOption,
    pub name: Option<&'static str>,
    pub legacy_name: Option<&'static str>,
}

