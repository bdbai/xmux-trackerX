# trackerX
Tracker info collector

## Request Format
### Request Endpoint
`http://[ip]/tracker_record`
### Request Method
`POST`
### Request Header
- `X-Device-Id`: current device ID
### Request Body
__One group__:
- `<ssid>\n`
- `<channel>\n`
- `<rssi>\n`
- `<bssid>`

__Groups are separated by `\n`.__
