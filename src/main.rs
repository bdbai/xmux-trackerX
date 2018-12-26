#![feature(proc_macro_hygiene, decl_macro)]

#[macro_use]
extern crate mysql;
extern crate reqwest;
extern crate rocket;
use reqwest::{header, Client, StatusCode};
use rocket::handler::Outcome;
use rocket::http::{Method, Status};
use rocket::response::Response;
use rocket::{Data, Route, State};
use std::boxed::Box;
use std::collections::{
    hash_map::Entry::{Occupied, Vacant},
    HashMap,
};
use std::env;
use std::error::Error;
use std::io::Cursor;
use std::io::Read;
use std::iter::FromIterator;
use std::sync::{Arc, RwLock};
mod hotspot;
use hotspot::Hotspot;
mod cache;
use cache::Cache;
use mysql as my;

fn parse_hotspots<'a>(data: &'a String) -> Result<Vec<Hotspot>, &'static str> {
    let strs = data.lines()
        .collect::<Vec<&str>>();
    strs
        .chunks_exact(4)
        .map(|buf| Hotspot::parse(buf))
        .collect()
}

fn prepare_hotspot(
    hotspot: &Hotspot,
    cache: &RwLock<Cache>,
    pool: &my::Pool,
) -> Result<i32, Box<Error>> {
    {
        let cache = cache.read().unwrap();
        match cache.hotspots.get(&hotspot.bssid) {
            Some(i) => Some(*i), // Cached already
            None => match cache.pending_hotspots.get(&hotspot.bssid) {
                Some(o) => Some(**o.read().unwrap()), // Another thread will store this device_id
                None => None,                         // Store and cache
            },
        }
    }
    .map(|i| Ok(i))
    .unwrap_or_else(|| {
        let arc = Arc::from(RwLock::from(Box::from(0)));
        let mut result = (*arc).write().unwrap();
        {
            let mut cache = cache.write().unwrap();
            match cache.pending_hotspots.entry(hotspot.bssid.to_string()) {
                Occupied(e) => Some(e.get().clone()), // Some other thread has already started storing
                Vacant(e) => {
                    e.insert(arc.clone());
                    None
                }
            }
        }
        .map(|l| Ok(**l.read().unwrap()))
        .unwrap_or_else(|| {
            let mut conn = pool.get_conn()?;
            let res = conn.prep_exec(
                "INSERT INTO `tracker_hotspot` (`ssid`, `bssid`, `channel`) VALUES (:ssid, :bssid, :channel)",
                params! {
                    "ssid" => &hotspot.ssid,
                    "bssid" => &hotspot.bssid,
                    "channel" => hotspot.channel
                },
            )?;
            let id = res.last_insert_id() as i32;
            let mut cache = cache.write().unwrap();
            cache.pending_hotspots.remove(&hotspot.bssid);
            cache.hotspots.insert(hotspot.bssid.to_string(), id);
            **result = id;
            println!("New hotspot {} with ID={}", hotspot.bssid, id);
            Ok(id)
        })
    })
}

fn prepare_device(
    device_id: String,
    cache: &RwLock<Cache>,
    pool: &my::Pool,
) -> Result<i32, Box<Error>> {
    {
        let cache = cache.read().unwrap();
        match cache.devices.get(&device_id) {
            Some(i) => Some(*i), // Cached already
            None => match cache.pending_devices.get(&device_id) {
                Some(o) => Some(**o.read().unwrap()), // Another thread will store this device_id
                None => None,                         // Store and cache
            },
        }
    }
    .map(|id| Ok(id))
    .unwrap_or_else(|| {
        let arc = Arc::from(RwLock::from(Box::from(0)));
        let mut result = (*arc).write().unwrap();
        {
            let mut cache = cache.write().unwrap();
            match cache.pending_devices.entry(device_id.to_string()) {
                Occupied(e) => Some(e.get().clone()), // Some other thread has already started storing
                Vacant(e) => {
                    e.insert(arc.clone());
                    None
                }
            }
        }
        .map(|l| Ok(**l.read().unwrap()))
        .unwrap_or_else(|| {
            let mut conn = pool.get_conn()?;
            let res = conn.prep_exec(
                "INSERT INTO `tracker_device` (`device`, `created_at`) VALUES (:device, NOW())",
                params! { "device" => &device_id },
            )?;
            let id = res.last_insert_id() as i32;
            let mut cache = cache.write().unwrap();
            cache.pending_devices.remove(&device_id);
            cache.devices.insert(device_id.to_string(), id);
            **result = id;
            println!("New device {} with ID={}", device_id, id);
            Ok(id)
        })
    })
}

fn save_signal_info(
    device_id: i32,
    hotspot_id: i32,
    rssi: i32,
    client: &Client,
    influxdb_url: &str,
) -> Result<(), Box<Error>> {
    let mut res = client
        .post(influxdb_url)
        .body(format!(
            "signal,device={},hotspot={} rssi={}",
            device_id, hotspot_id, rssi
        ))
        .send()?;
    match res.status() {
        StatusCode::NO_CONTENT => Ok(()),
        _ => Err(Box::from(format!(
            "{}\n{}",
            res.status().as_str(),
            res.text()?
        ))),
    }
}

fn handle_data<'a, 'b>(
    device_id: &'b str,
    data: &'a String,
    cache: &RwLock<Cache>,
    pool: &my::Pool,
    client: &Client,
) -> Result<Result<usize, &'static str>, Box<Error>> {
    let influxdb_url = {
        let cache = cache.read().unwrap();
        cache.influxdb_url.clone()
    };
    let device_id = prepare_device(String::from(device_id), cache, pool)?;
    match parse_hotspots(data) {
        Ok(hotspots) => {
            let len = hotspots.len();
            hotspots
                .into_iter()
                .map(|h| {
                    let id = prepare_hotspot(&h, &cache, &pool)?;
                    save_signal_info(device_id, id, h.rssi, client, influxdb_url.as_str())
                })
                .collect::<Result<Vec<()>, Box<Error>>>()
                .map(|_| Ok(len))
        }
        Err(e) => Ok(Err(e)),
    }
}

fn index<'a>(req: &'a rocket::Request, data: Data) -> Outcome<'a> {
    let pool = req.guard::<State<my::Pool>>().unwrap();
    let cache = req.guard::<State<RwLock<Cache>>>().unwrap();
    let client = req.guard::<State<reqwest::Client>>().unwrap();
    let mut res = Response::new();
    res.set_raw_header("Content-Type", "text/plain");
    let len = req
        .headers()
        .get_one("Content-Length")
        .and_then(|l| l.parse::<i32>().ok())
        .unwrap();
    let device_id = req.headers().get_one("X-Device-Id").unwrap_or("").trim();
    if len > 1024 * 10 {
        res.set_sized_body(Cursor::new("entity too large"));
        res.set_status(Status::new(413, "Entity Too Large"));
    } else if req.method().as_str() == "POST" && len > 0 && !device_id.is_empty() {
        let mut buf = String::new();
        data.open().read_to_string(&mut buf).unwrap();
        match handle_data(device_id, &buf, &cache, &pool, &client) {
            Ok(Err(e)) => {
                res.set_sized_body(Cursor::new(e));
                res.set_status(Status::new(400, "Bad Request"));
            }
            Ok(Ok(cnt)) => {
                res.set_sized_body(Cursor::new(cnt.to_string()));
                res.set_status(Status::new(200, "Ok"));
            }
            Err(e) => {
                eprintln!("{}", e);
                res.set_sized_body(Cursor::new(e.to_string()));
                res.set_status(Status::new(500, "Internal Server Error"));
            }
        }
    } else {
        res.set_sized_body(Cursor::new("Malformed request"));
        res.set_status(Status::new(400, "Bad Request"));
    }
    Outcome::from(req, res)
}

fn query_device_ids(pool: &my::Pool) -> HashMap<String, i32> {
    let devices: Vec<(String, i32)> = pool
        .prep_exec("SELECT `id`, `device` FROM `tracker_device`", ())
        .unwrap()
        .map(|d| d.unwrap())
        .map(|row| {
            let (id, device) = my::from_row(row);
            (device, id)
        })
        .collect();
    println!("{:?}", devices);
    HashMap::from_iter(devices)
}

fn query_hotspot_ids(pool: &my::Pool) -> HashMap<String, i32> {
    let hotspots: Vec<(String, i32)> = pool
        .prep_exec("SELECT `id`, `bssid` FROM `tracker_hotspot`", ())
        .unwrap()
        .map(|d| d.unwrap())
        .map(|row| {
            let (id, bssid) = my::from_row(row);
            (bssid, id)
        })
        .collect();
    println!("{:?}", hotspots);
    HashMap::from_iter(hotspots)
}

fn main() {
    let influxdb_url = env::var("INFLUXDB_URL").unwrap() + "/write?db=tracker";
    let mut headers = header::HeaderMap::new();
    headers.insert(
        "User-Agent",
        header::HeaderValue::from_static("Tracker/2.0 bdbai"),
    );
    let client = Client::builder().default_headers(headers).build().unwrap();

    let mut builder = my::OptsBuilder::new();
    builder
        .ip_or_hostname(Some(env::var("MYSQL_HOST").unwrap()))
        .user(Some("tracker"))
        .pass(Some("trackerX"))
        .db_name(Some("tracker"));
    let opt = my::Opts::from(builder);
    let pool = my::Pool::new(opt).unwrap();
    let routes = vec![
        Route::new(Method::Post, "/<path..>", index),
        Route::new(Method::Post, "/", index),
    ];
    let cache = Cache {
        influxdb_url: Arc::from(influxdb_url),
        hotspots: query_hotspot_ids(&pool),
        devices: query_device_ids(&pool),
        pending_devices: HashMap::new(),
        pending_hotspots: HashMap::new(),
    };
    rocket::ignite()
        .manage(pool)
        .manage(RwLock::new(cache))
        .manage(client)
        .mount("/", routes)
        .launch();
}
