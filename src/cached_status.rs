use std::error::Error;
use std::sync::{Arc, RwLock};

pub enum CachedStatus {
    Cached(i32),
    Storing(Arc<RwLock<i32>>),
    NotCached,
}

impl CachedStatus {
    pub fn resolve(self) -> Option<Result<i32, Box<Error>>> {
        match self {
            CachedStatus::Cached(i) => Some(Ok(i)),
            CachedStatus::Storing(l) => Some(
                l.read()
                    .map(|i| *i)
                    .map_err(|_| Box::from("Cache RwLock poisoned")),
            ),
            CachedStatus::NotCached => None,
        }
    }
}
