
#define LDR_PIN 35

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(LDR_PIN, ADC_11db);
}

void loop() {
    int light = analogRead(LDR_PIN);
    Serial.println(light);  // 0–4095
    delay(500);
}
