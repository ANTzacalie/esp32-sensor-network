package org.lab.sensormonitoring

import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.runApplication
import org.springframework.boot.ApplicationRunner
import org.springframework.context.annotation.Bean
import org.springframework.jdbc.core.JdbcTemplate
import org.springframework.transaction.PlatformTransactionManager
import org.springframework.transaction.support.TransactionTemplate
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder
import java.nio.file.Files
import java.nio.file.Path
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import kotlin.math.round

/** Application entry point for the ξensorMon website and REST API. */
@SpringBootApplication
class SensorMonitoringApplication

/** Prepares the SQLite schema, adds sample sensor history, and creates the local demo account. */
@org.springframework.context.annotation.Configuration
class DatabaseSetup {
    @Bean
    fun seedDatabase(jdbc: JdbcTemplate, transactionManager: PlatformTransactionManager) = ApplicationRunner {
        Files.createDirectories(Path.of("data"))
        TransactionTemplate(transactionManager).executeWithoutResult {
        // Add GPS columns to databases created before map support, then give demo modules sample locations.
        val moduleColumns = jdbc.queryForList("PRAGMA table_info(sensor_module)").map { it["name"]?.toString() }.toSet()
        if ("latitude" !in moduleColumns) jdbc.execute("ALTER TABLE sensor_module ADD COLUMN latitude REAL")
        if ("longitude" !in moduleColumns) jdbc.execute("ALTER TABLE sensor_module ADD COLUMN longitude REAL")
        jdbc.update("""UPDATE sensor_module SET latitude=44.4268 + (bank_id - 6.5) * 0.0021 + (module_number % 5 - 2) * 0.00035,
            longitude=26.1025 + (bank_id - 6.5) * 0.0026 + (module_number % 7 - 3) * 0.0004
            WHERE latitude IS NULL AND longitude IS NULL""")
        if (jdbc.queryForObject("SELECT COUNT(*) FROM sensor_bank", Int::class.java) == 0) {
            // Seed 12 banks, each with 25 example modules and a day of readings so the UI has content immediately.
            repeat(12) { bankIndex ->
                val bank = bankIndex + 1
                jdbc.update("INSERT INTO sensor_bank(id, name) VALUES (?, ?)", bank, "Sensor bank %02d".format(bank))
                repeat(25) { moduleIndex ->
                    val number = moduleIndex + 1
                    val status = if ((bank + number) % 19 == 0) "offline" else "online"
                    val latitude = 44.4268 + (bank - 6.5) * 0.0021 + (number % 5 - 2) * 0.00035
                    val longitude = 26.1025 + (bank - 6.5) * 0.0026 + (number % 7 - 3) * 0.0004
                    jdbc.update("INSERT INTO sensor_module(bank_id, module_number, name, status, latitude, longitude) VALUES (?, ?, ?, ?, ?, ?)", bank, number, "Module %02d".format(number), status, latitude, longitude)
                    val moduleId = jdbc.queryForObject("SELECT id FROM sensor_module WHERE bank_id=? AND module_number=?", Int::class.java, bank, number)!!
                    repeat(24) { hourAgo ->
                        val wave = kotlin.math.sin((24 - hourAgo + number * 0.7 + bank) / 4.0)
                        val temp = round((21.5 + wave * 3.2 + ((number % 5) - 2) * 0.35) * 10) / 10
                        val pressure = round((1013.2 + kotlin.math.cos((24 - hourAgo + bank) / 5.0) * 5 + (number % 7) * 0.4) * 10) / 10
                        val humidity = round((48 + kotlin.math.sin((24 - hourAgo + number) / 6.0) * 13 + (bank % 4)) * 10) / 10
                        val light = round((380 + kotlin.math.max(0.0, kotlin.math.sin((24 - hourAgo - 6) / 24.0 * Math.PI)) * 520 + (number % 8) * 11) * 10) / 10
                        val recordedAt = LocalDateTime.now().minusHours(hourAgo.toLong()).format(DateTimeFormatter.ISO_LOCAL_DATE_TIME)
                        jdbc.update("INSERT INTO sensor_reading(module_id, recorded_at, temperature, pressure, humidity, movement, light) VALUES (?, ?, ?, ?, ?, ?, ?)", moduleId, recordedAt, temp, pressure, humidity, if ((hourAgo + number + bank) % 17 == 0) 1 else 0, light)
                    }
                }
            }
        }
        if (jdbc.queryForObject("SELECT COUNT(*) FROM app_user WHERE email=?", Int::class.java, "admin@sensornet.local") == 0) {
            jdbc.update("INSERT INTO app_user(email, password_hash) VALUES (?, ?)", "admin@sensornet.local", BCryptPasswordEncoder().encode("SensorNet2026!"))
        }
        }
    }
}

fun main(args: Array<String>) {
    Files.createDirectories(Path.of("data"))
    runApplication<SensorMonitoringApplication>(*args)
}
