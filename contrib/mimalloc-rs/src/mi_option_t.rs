use crate::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MiOption {
    ShowErrors,
    ShowStats,
    Verbose,
    EagerCommit,
    ArenaEagerCommit,
    PurgeDecommits,
    AllowLargeOsPages,
    ReserveHugeOsPages,
    ReserveHugeOsPagesAt,
    ReserveOsMemory,
    DeprecatedSegmentCache,
    DeprecatedPageReset,
    AbandonedPagePurge,
    DeprecatedSegmentReset,
    EagerCommitDelay,
    PurgeDelay,
    UseNumaNodes,
    DisallowOsAlloc,
    OsTag,
    MaxErrors,
    MaxWarnings,
    DeprecatedMaxSegmentReclaim,
    DestroyOnExit,
    ArenaReserve,
    ArenaPurgeMult,
    DeprecatedPurgeExtendDelay,
    DisallowArenaAlloc,
    RetryOnOom,
    VisitAbandoned,
    GuardedMin,
    GuardedMax,
    GuardedPrecise,
    GuardedSampleRate,
    GuardedSampleSeed,
    GenericCollect,
    PageReclaimOnFree,
    PageFullRetain,
    PageMaxCandidates,
    MaxVabits,
    PagemapCommit,
    PageCommitOnDemand,
    PageMaxReclaim,
    PageCrossThreadMaxReclaim,
    MiOptionLast,
}

pub struct MiOptionAliases;

impl MiOptionAliases {
    pub const LARGE_OS_PAGES: MiOption = MiOption::AllowLargeOsPages;
    pub const EAGER_REGION_COMMIT: MiOption = MiOption::ArenaEagerCommit;
    pub const RESET_DECOMMITS: MiOption = MiOption::PurgeDecommits;
    pub const RESET_DELAY: MiOption = MiOption::PurgeDelay;
    pub const ABANDONED_PAGE_RESET: MiOption = MiOption::AbandonedPagePurge;
    pub const LIMIT_OS_ALLOC: MiOption = MiOption::DisallowOsAlloc;
}

