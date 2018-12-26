use std::str::FromStr;
use std::string::String;

pub struct Hotspot {
    pub ssid: String,
    pub channel: i32,
    pub rssi: i32,
    pub bssid: String,
}

impl Hotspot {
    pub fn parse<'a>(lines: &[&'a str]) -> Result<Hotspot, &'static str> {
        if lines.len() != 4 {
            Err("No enough data")
        } else {
            let channel = lines[1].parse::<i32>().map_err(|_| "Wrong channel");
            let rssi = lines[2].parse::<i32>().map_err(|_| "Wrong rssi");
            channel.and(rssi).map(|rssi| Hotspot {
                ssid: String::from_str(lines[0]).unwrap(),
                bssid: String::from_str(lines[3]).unwrap(),
                channel: channel.unwrap(),
                rssi,
            })
        }
    }
}
