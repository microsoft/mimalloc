use crate::*;

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MiAnsiColor {
    Black = 30,
    Maroon,
    DarkGreen,
    Orange,
    Navy,
    Purple,
    Teal,
    Gray,
    DarkGray = 90,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
}

