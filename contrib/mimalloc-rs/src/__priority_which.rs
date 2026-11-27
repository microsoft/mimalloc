use crate::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PriorityWhich {
    Process = 0,
    Pgrp = 1,
    User = 2,
}

impl PriorityWhich {
    pub const PRIO_PROCESS: Self = Self::Process;
    pub const PRIO_PGRP: Self = Self::Pgrp;
    pub const PRIO_USER: Self = Self::User;
}

