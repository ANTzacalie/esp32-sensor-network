# Sensor matrix ingestion

Modules can send one reading or a batch of readings to the server. Batch requests use JSON and a table whose first row contains column names. The server matches columns by name, so extra metadata columns such as `size`, `packet_size`, `battery`, or `rssi` are accepted and ignored.

## Batch endpoint

`POST /api/ingest/matrix`

Required header: `X-Sensor-Key: <configured key>`

Set the key with `SENSOR_API_KEY` before starting the server. The local development fallback is `dev-sensor-key-change-me`.

Example matrix (first row is the table header):

```json
{
  "matrix": [
    ["sensor bank", "sensor number", "time and date on sensor", "temperature C", "pressure hPa", "humidity %", "movement", "light lux", "latitude", "longitude", "size"],
    [1, 5, "2026-11-17T12:30:00", 22.4, 1012.8, 48.5, 0, 420, 44.4268, 26.1025, 64],
    [1, 5, "2026-11-17T12:31:00", 22.5, 1012.7, 48.2, 1, 425, 44.4268, 26.1025, 64]
  ]
}
```

The same table can be sent as a raw JSON array, or as `{"headers": [...], "rows": [[...], ...]}`. A row represents one timestamped sample from one module. One batch may contain rows for multiple banks and modules. Rows are validated and stored atomically; if one row is invalid, the batch is rejected without saving any rows.

Column names are case-insensitive; spaces, underscores, and punctuation do not matter. Recognized names include:

| Field | Accepted examples |
| --- | --- |
| Bank | `sensor bank`, `bank_id` |
| Module number | `sensor number`, `module_number`, `sensor_id` |
| Timestamp | `time and date on sensor`, `timestamp`, `recorded_at`; or separate `date` and `time` columns |
| Temperature | `temperature`, `temp_c` |
| Pressure | `pressure`, `pressure_hpa` |
| Humidity | `humidity`, `humidity_pct` |
| Movement | `movement`, `motion` (number or boolean text) |
| Light | `light`, `light_lux`, `illuminance` |
| Latitude | `latitude`, `lat`, `gps_latitude` (optional; send with longitude) |
| Longitude | `longitude`, `lon`, `lng`, `gps_longitude` (optional; send with latitude) |

Timestamps support ISO date/time strings, `yyyy-MM-dd HH:mm:ss`, `dd/MM/yyyy HH:mm:ss`, ISO date-only values, and Unix epoch seconds or milliseconds. The response reports the number of accepted rows.

## Request example

```bash
curl -X POST http://localhost:8080/api/ingest/matrix \
  -H 'Content-Type: application/json' \
  -H 'X-Sensor-Key: dev-sensor-key-change-me' \
  --data-binary @sensor-batch.json
```

The existing single-reading endpoint remains available at `POST /api/ingest/{bankId}/{moduleNumber}`.

The single-reading endpoint also accepts optional `latitude` and `longitude` fields in its JSON body. A module's last reported coordinates are returned by `GET /api/banks/{bankId}/modules` and shown in the dashboard's map mode. Existing databases are migrated at startup. Fresh demo installs receive sample locations near Bucharest until real module coordinates arrive.
