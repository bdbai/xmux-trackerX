use std::collections::HashMap;
use std::sync::{Arc, RwLock};

pub struct Cache {
    pub influxdb_url: Arc<String>,
    pub hotspots: HashMap<String, i32>,
    pub devices: HashMap<String, i32>,
    pub pending_devices: HashMap<String, Arc<RwLock<Box<i32>>>>,
    pub pending_hotspots: HashMap<String, Arc<RwLock<Box<i32>>>>,
}
