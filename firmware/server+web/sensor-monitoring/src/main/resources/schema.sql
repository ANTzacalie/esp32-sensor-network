-- Browser accounts are stored separately from device API keys.
CREATE TABLE IF NOT EXISTS app_user (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  email TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL
);
-- Bank IDs and labels used in the dashboard's bank selector.
CREATE TABLE IF NOT EXISTS sensor_bank (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL
);
-- Up to 25 modules per bank; GPS coordinates hold the latest location each module reported.
CREATE TABLE IF NOT EXISTS sensor_module (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  bank_id INTEGER NOT NULL REFERENCES sensor_bank(id),
  module_number INTEGER NOT NULL,
  name TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'online',
  latitude REAL,
  longitude REAL,
  UNIQUE(bank_id, module_number)
);
-- Sensor samples are append-only so charts and exports can use the full history.
CREATE TABLE IF NOT EXISTS sensor_reading (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  module_id INTEGER NOT NULL REFERENCES sensor_module(id),
  recorded_at TEXT NOT NULL,
  temperature REAL NOT NULL,
  pressure REAL NOT NULL,
  humidity REAL NOT NULL,
  movement INTEGER NOT NULL,
  light REAL NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_readings_module_time ON sensor_reading(module_id, recorded_at);
