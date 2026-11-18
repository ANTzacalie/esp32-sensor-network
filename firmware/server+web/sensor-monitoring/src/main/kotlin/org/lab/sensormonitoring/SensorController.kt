package org.lab.sensormonitoring

import jakarta.servlet.http.HttpSession
import org.springframework.http.HttpStatus
import org.springframework.jdbc.core.JdbcTemplate
import org.springframework.web.bind.annotation.*
import org.springframework.web.server.ResponseStatusException
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder
import org.springframework.beans.factory.annotation.Value
import org.springframework.transaction.annotation.Transactional
import tools.jackson.databind.JsonNode
import java.time.Instant
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.OffsetDateTime
import java.time.ZoneOffset
import java.time.format.DateTimeParseException
import java.time.format.DateTimeFormatter

data class Credentials(val email: String, val password: String)
data class BankDto(val id: Int, val name: String, val modules: Int, val online: Int)
data class ModuleDto(val id: Int, val bankId: Int, val number: Int, val name: String, val status: String, val recordedAt: String?, val temperature: Double?, val pressure: Double?, val humidity: Double?, val movement: Int?, val light: Double?, val latitude: Double?, val longitude: Double?)
data class ReadingDto(val recordedAt: String, val temperature: Double, val pressure: Double, val humidity: Double, val movement: Int, val light: Double)
data class SensorPayload(val temperature: Double, val pressure: Double, val humidity: Double, val movement: Int, val light: Double, val latitude: Double? = null, val longitude: Double? = null)

/** HTTP endpoints for user sessions, sensor dashboards, and device data uploads. */
@RestController
@RequestMapping("/api")
class SensorController(private val jdbc: JdbcTemplate, @Value("\${sensor.api-key}") private val sensorApiKey: String) {
    private val passwordEncoder = BCryptPasswordEncoder()

    /** Checks the demo/user account and starts a browser session. */
    @PostMapping("/login")
    fun login(@RequestBody credentials: Credentials, session: HttpSession): Map<String, Any> {
        val email = credentials.email.trim().lowercase()
        val hash = jdbc.query("SELECT password_hash FROM app_user WHERE email = ?", { rs, _ -> rs.getString(1) }, email).firstOrNull()
        if (hash == null || !passwordEncoder.matches(credentials.password, hash)) {
            throw ResponseStatusException(HttpStatus.UNAUTHORIZED, "Email or password is incorrect")
        }
        session.setAttribute("userEmail", email)
        return mapOf("email" to email)
    }

    @PostMapping("/logout")
    fun logout(session: HttpSession): Map<String, String> { session.invalidate(); return mapOf("status" to "ok") }

    @GetMapping("/session")
    fun session(session: HttpSession): Map<String, Any?> = mapOf("authenticated" to (session.getAttribute("userEmail") != null), "email" to session.getAttribute("userEmail"))

    /** Return the banks and online module counts shown in the sidebar. */
    @GetMapping("/banks")
    fun banks(session: HttpSession): List<BankDto> {
        requireLogin(session)
        return jdbc.query("SELECT b.id, b.name, COUNT(m.id) modules, SUM(CASE WHEN m.status='online' THEN 1 ELSE 0 END) online FROM sensor_bank b LEFT JOIN sensor_module m ON m.bank_id=b.id GROUP BY b.id ORDER BY b.id") { rs, _ ->
            BankDto(rs.getInt("id"), rs.getString("name"), rs.getInt("modules"), rs.getInt("online"))
        }
    }

    /** Return one bank's 25 modules together with their latest reading and GPS position. */
    @GetMapping("/banks/{bankId}/modules")
    fun modules(@PathVariable bankId: Int, session: HttpSession): List<ModuleDto> {
        requireLogin(session)
        return jdbc.query("""SELECT m.id, m.bank_id, m.module_number, m.name, m.status, m.latitude, m.longitude, r.recorded_at, r.temperature, r.pressure, r.humidity, r.movement, r.light
            FROM sensor_module m LEFT JOIN sensor_reading r ON r.id=(SELECT r2.id FROM sensor_reading r2 WHERE r2.module_id=m.id ORDER BY r2.recorded_at DESC LIMIT 1)
            WHERE m.bank_id=? ORDER BY m.module_number""", { rs, _ ->
            ModuleDto(rs.getInt("id"), rs.getInt("bank_id"), rs.getInt("module_number"), rs.getString("name"), rs.getString("status"), rs.getString("recorded_at"), rs.getObject("temperature")?.let { rs.getDouble("temperature") }, rs.getObject("pressure")?.let { rs.getDouble("pressure") }, rs.getObject("humidity")?.let { rs.getDouble("humidity") }, rs.getObject("movement")?.let { rs.getInt("movement") }, rs.getObject("light")?.let { rs.getDouble("light") }, rs.getObject("latitude")?.let { rs.getDouble("latitude") }, rs.getObject("longitude")?.let { rs.getDouble("longitude") })
        }, bankId)
    }

    /** Return recent history points for the selected module's chart. */
    @GetMapping("/modules/{moduleId}/history")
    fun history(@PathVariable moduleId: Int, @RequestParam(defaultValue = "24") hours: Int, session: HttpSession): List<ReadingDto> {
        requireLogin(session)
        return jdbc.query("SELECT recorded_at, temperature, pressure, humidity, movement, light FROM sensor_reading WHERE module_id=? ORDER BY recorded_at DESC LIMIT ?", { rs, _ ->
            ReadingDto(rs.getString("recorded_at"), rs.getDouble("temperature"), rs.getDouble("pressure"), rs.getDouble("humidity"), rs.getInt("movement"), rs.getDouble("light"))
        }, moduleId, hours.coerceIn(1, 168)).reversed()
    }

    /** Accept a JSON table, validate every row, then store the whole batch as one transaction. */
    @Transactional
    @PostMapping("/ingest/matrix")
    fun ingestMatrix(
        @RequestHeader("X-Sensor-Key", required = false) key: String?,
        @RequestBody body: JsonNode
    ): Map<String, Any> {
        requireSensorKey(key)
        val (headerNode, rowNodes) = extractTable(body)
        // Normalize headings so names like "GPS Latitude" and "gps_latitude" map to the same field.
        val columns = headerNode.mapIndexed { index, node -> normalizeHeader(node.asText()) to index }.toMap()
        val bankColumn = column(columns, "bank", "sensorbank", "bankid")
        val moduleColumn = column(columns, "sensornumber", "modulenumber", "sensorid", "modulenumberid")
        val timestampColumn = column(columns, "timestamp", "datetime", "sensordatetime", "timeanddateonsensor", "recordedat")
        val dateColumn = column(columns, "date", "sensor date", "dateonsensor")
        val timeColumn = column(columns, "time", "sensortime", "timeonsensor")
        val temperatureColumn = column(columns, "temperature", "temp", "temperaturec", "tempc")
        val pressureColumn = column(columns, "pressure", "pressurehpa", "barometricpressure")
        val humidityColumn = column(columns, "humidity", "humiditypercent", "humiditypct", "humidityrh")
        val movementColumn = column(columns, "movement", "motion", "movementevents", "motionevents")
        val lightColumn = column(columns, "light", "lightlux", "illuminance", "lux")
        val latitudeColumn = column(columns, "latitude", "lat", "gpslatitude", "locationlatitude")
        val longitudeColumn = column(columns, "longitude", "lon", "lng", "gpslongitude", "locationlongitude")
        val required = listOf(bankColumn, moduleColumn, temperatureColumn, pressureColumn, humidityColumn, movementColumn, lightColumn)
        if (required.any { it == null } || (timestampColumn == null && dateColumn == null)) {
            throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Table needs sensor bank, sensor number, date/time, temperature, pressure, humidity, movement, and light columns")
        }
        if (rowNodes.isEmpty() || rowNodes.size > 5000) {
            throw ResponseStatusException(HttpStatus.BAD_REQUEST, "A matrix must contain between 1 and 5000 data rows")
        }

        data class ParsedRow(val moduleId: Int, val timestamp: String, val reading: SensorPayload, val latitude: Double?, val longitude: Double?)
        // Validate the entire batch before writing any rows so a bad cell cannot leave partial data.
        val parsed = rowNodes.mapIndexed { rowIndex, row ->
            if (!row.isArray || row.size() < headerNode.size) {
                throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Row ${rowIndex + 1} does not have enough cells for the header")
            }
            fun cell(index: Int?): String? = index?.let { row.get(it) }?.takeUnless { it.isNull }?.asText()?.trim()?.takeIf { it.isNotEmpty() }
            val bankId = cell(bankColumn)?.toIntOrNull() ?: throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid sensor bank in row ${rowIndex + 1}")
            val moduleNumber = cell(moduleColumn)?.toIntOrNull() ?: throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid sensor number in row ${rowIndex + 1}")
            val timestampText = if (timestampColumn != null) cell(timestampColumn) else listOfNotNull(cell(dateColumn), cell(timeColumn)).joinToString(" ").takeIf { it.isNotBlank() }
            val timestamp = parseSensorTimestamp(timestampText ?: throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Missing date/time in row ${rowIndex + 1}"), rowIndex)
            val reading = SensorPayload(
                temperature = cell(temperatureColumn)?.toDoubleOrNull() ?: invalidCell(rowIndex, "temperature"),
                pressure = cell(pressureColumn)?.toDoubleOrNull() ?: invalidCell(rowIndex, "pressure"),
                humidity = cell(humidityColumn)?.toDoubleOrNull() ?: invalidCell(rowIndex, "humidity"),
                movement = cell(movementColumn)?.let { parseMovement(it) } ?: invalidCell(rowIndex, "movement"),
                light = cell(lightColumn)?.toDoubleOrNull() ?: invalidCell(rowIndex, "light")
            )
            validateReading(reading, rowIndex)
            val latitude = cell(latitudeColumn)?.toDoubleOrNull()
            val longitude = cell(longitudeColumn)?.toDoubleOrNull()
            validateCoordinates(latitude, longitude, rowIndex)
            val moduleId = jdbc.query("SELECT id FROM sensor_module WHERE bank_id=? AND module_number=?", { rs, _ -> rs.getInt(1) }, bankId, moduleNumber).firstOrNull()
                ?: throw ResponseStatusException(HttpStatus.NOT_FOUND, "Sensor module $moduleNumber was not found in bank $bankId")
            ParsedRow(moduleId, timestamp, reading, latitude, longitude)
        }

        jdbc.batchUpdate("INSERT INTO sensor_reading(module_id, recorded_at, temperature, pressure, humidity, movement, light) VALUES (?, ?, ?, ?, ?, ?, ?)", parsed, 250) { statement, row ->
            statement.setInt(1, row.moduleId)
            statement.setString(2, row.timestamp)
            statement.setDouble(3, row.reading.temperature)
            statement.setDouble(4, row.reading.pressure)
            statement.setDouble(5, row.reading.humidity)
            statement.setInt(6, row.reading.movement)
            statement.setDouble(7, row.reading.light)
        }
        parsed.groupBy { it.moduleId }.forEach { (moduleId, rows) ->
            val location = rows.lastOrNull { it.latitude != null }
            if (location != null) jdbc.update("UPDATE sensor_module SET status='online', latitude=?, longitude=? WHERE id=?", location.latitude, location.longitude, moduleId)
            else jdbc.update("UPDATE sensor_module SET status='online' WHERE id=?", moduleId)
        }
        return mapOf("status" to "accepted", "rowsAccepted" to parsed.size)
    }

    /** Accept one reading directly from a module; optional GPS values update its saved map pin. */
    @PostMapping("/ingest/{bankId}/{moduleNumber}")
    fun ingest(
        @PathVariable bankId: Int,
        @PathVariable moduleNumber: Int,
        @RequestHeader("X-Sensor-Key", required = false) key: String?,
        @RequestBody reading: SensorPayload
    ): Map<String, Any> {
        requireSensorKey(key)
        validateReading(reading)
        validateCoordinates(reading.latitude, reading.longitude)
        val moduleId = jdbc.query("SELECT id FROM sensor_module WHERE bank_id=? AND module_number=?", { rs, _ -> rs.getInt(1) }, bankId, moduleNumber).firstOrNull()
            ?: throw ResponseStatusException(HttpStatus.NOT_FOUND, "Sensor module was not found")
        val timestamp = LocalDateTime.now().format(DateTimeFormatter.ISO_LOCAL_DATE_TIME)
        jdbc.update("INSERT INTO sensor_reading(module_id, recorded_at, temperature, pressure, humidity, movement, light) VALUES (?, ?, ?, ?, ?, ?, ?)", moduleId, timestamp, reading.temperature, reading.pressure, reading.humidity, reading.movement, reading.light)
        if (reading.latitude != null) jdbc.update("UPDATE sensor_module SET status='online', latitude=?, longitude=? WHERE id=?", reading.latitude, reading.longitude, moduleId)
        else jdbc.update("UPDATE sensor_module SET status='online' WHERE id=?", moduleId)
        return mapOf("status" to "accepted", "bankId" to bankId, "moduleNumber" to moduleNumber, "recordedAt" to timestamp)
    }

    private fun requireLogin(session: HttpSession) {
        if (session.getAttribute("userEmail") == null) throw ResponseStatusException(HttpStatus.UNAUTHORIZED, "Please sign in")
    }

    private fun requireSensorKey(key: String?) {
        if (key != sensorApiKey) throw ResponseStatusException(HttpStatus.UNAUTHORIZED, "Invalid sensor API key")
    }

    private fun validateReading(reading: SensorPayload, rowIndex: Int? = null) {
        if (reading.temperature !in -80.0..100.0 || reading.pressure !in 100.0..1200.0 || reading.humidity !in 0.0..100.0 || reading.movement < 0 || reading.light < 0.0) {
            val rowText = rowIndex?.let { " in row ${it + 1}" } ?: ""
            throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Reading values are outside supported ranges$rowText")
        }
    }

    /** Require GPS coordinates as a pair and check they are valid latitude/longitude values. */
    private fun validateCoordinates(latitude: Double?, longitude: Double?, rowIndex: Int? = null) {
        val rowText = rowIndex?.let { " in row ${it + 1}" } ?: ""
        if ((latitude == null) != (longitude == null)) throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Latitude and longitude must be provided together$rowText")
        if ((latitude != null && latitude !in -90.0..90.0) || (longitude != null && longitude !in -180.0..180.0)) {
            throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Coordinates are outside valid ranges$rowText")
        }
    }

    private fun parseMovement(value: String): Int = when (value.lowercase()) {
        "true", "yes", "on" -> 1
        "false", "no", "off" -> 0
        else -> value.toIntOrNull() ?: throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Movement must be a non-negative number or boolean")
    }

    /** Normalize common device timestamp formats to an ISO local date/time string for SQLite. */
    private fun parseSensorTimestamp(value: String, rowIndex: Int): String {
        val timestamp = try {
            runCatching { LocalDateTime.parse(value, DateTimeFormatter.ISO_DATE_TIME) }.getOrNull()
                ?: runCatching { OffsetDateTime.parse(value, DateTimeFormatter.ISO_DATE_TIME).toLocalDateTime() }.getOrNull()
                ?: runCatching { LocalDateTime.parse(value, DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")) }.getOrNull()
                ?: runCatching { LocalDateTime.parse(value, DateTimeFormatter.ofPattern("dd/MM/yyyy HH:mm:ss")) }.getOrNull()
                ?: runCatching { LocalDate.parse(value, DateTimeFormatter.ISO_DATE).atStartOfDay() }.getOrNull()
                ?: run {
                    val epoch = value.toLongOrNull() ?: throw DateTimeParseException("Unsupported timestamp", value, 0)
                    if (value.length >= 13) LocalDateTime.ofInstant(Instant.ofEpochMilli(epoch), ZoneOffset.UTC)
                    else LocalDateTime.ofInstant(Instant.ofEpochSecond(epoch), ZoneOffset.UTC)
                }
        } catch (_: DateTimeParseException) {
            throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid sensor date/time in row ${rowIndex + 1}: $value")
        }
        return timestamp.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME)
    }

    /** Read either a raw matrix, a matrix property, or a separate headers/rows envelope. */
    private fun extractTable(body: JsonNode): Pair<List<JsonNode>, List<JsonNode>> {
        if (body.isArray) {
            if (body.size() < 2 || !body[0].isArray) throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Send a matrix with a header row and at least one data row")
            return body[0].toList() to body.drop(1)
        }
        if (body.isObject) {
            val matrix = body.get("matrix")
            if (matrix?.isArray == true && matrix.size() >= 2 && matrix[0].isArray) return matrix[0].toList() to matrix.drop(1)
            val headers = body.get("headers")
            val rows = body.get("rows")
            if (headers?.isArray == true && rows?.isArray == true) return headers.toList() to rows.toList()
        }
        throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Expected a JSON matrix or an object containing headers and rows")
    }

    private fun normalizeHeader(value: String): String = value.lowercase().filter { it.isLetterOrDigit() }

    private fun column(columns: Map<String, Int>, vararg aliases: String): Int? = aliases.firstNotNullOfOrNull { alias -> columns[normalizeHeader(alias)] }

    private fun invalidCell(rowIndex: Int, name: String): Nothing = throw ResponseStatusException(HttpStatus.BAD_REQUEST, "Missing or invalid $name in row ${rowIndex + 1}")
}
