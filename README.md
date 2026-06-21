# DC Motor Predictive Maintenance using AI

An AI-based extension to an IoT machine health monitoring system — predicting motor fault levels (Normal, Watch, Warning, Critical, Failure) from vibration and temperature sensor data, built on top of an existing ESP32-based rule classification system.

## Overview

This project upgrades a rule-based embedded fault detection system (ESP32 + MPU6050 + DS18B20) into a machine learning model that learns the same fault-classification logic from sensor data, as a foundation for future trend-based predictive maintenance.

**Hardware reference system:**
- ESP32 Dev Board
- MPU6050/6500 IMU — vibration sensing (I2C)
- DS18B20 — motor temperature sensing
- OLED display, LEDs, and buzzer for real-time status alerts
- ThingSpeak for cloud data logging

## Fault Classification Levels

| Level | Vibration RMS | Temperature |
|---|---|---|
| Normal | < 0.30 g | < 35°C |
| Watch | ≥ 0.30 g | ≥ 35°C |
| Warning | ≥ 0.60 g | ≥ 55°C |
| Critical | ≥ 1.00 g | ≥ 65°C |
| Failure | ≥ 1.40 g | ≥ 85°C |

These thresholds match the firmware's `classifyFault()` logic exactly — the worse of the two readings (vibration or temperature) determines the final fault level.

## Dataset

`dc_motor_predictive_maintenance_dataset.csv` — 6,654 simulated sensor readings across 22 test sessions (healthy runs + gradual degradation runs covering all 5 fault tiers), generated to reflect realistic small-DC-motor behavior while strictly following the firmware's threshold logic.

**Columns:** accelerometer (x/y/z, g), gyroscope (x/y/z, deg/s), vibration RMS (g), temperature (°C), fault level, fault name, and a severity score.

## Model

A **Random Forest Classifier** (scikit-learn) trained on the 8 sensor features to predict fault level.

**Results:**
- **99.70% accuracy** on held-out test data
- Precision/recall above 0.99 across all 5 fault classes
- Most influential features: `temperature_c` (47%) and `vibration_rms_g` (34%)

See `DC_Motor_Predictive_Maintenance_Training.ipynb` for the full training pipeline, confusion matrix, and feature importance analysis.

## How to Use

1. Open `DC_Motor_Predictive_Maintenance_Training.ipynb` in [Google Colab](https://colab.research.google.com)
2. Run all cells (uploads the dataset, trains the model, evaluates results)
3. Download the trained model (`dc_motor_fault_model.pkl`) for reuse

## Future Work

- Sequence-based prediction (using a rolling window of recent readings) to detect early degradation trends *before* a fault threshold is crossed — true predictive maintenance rather than single-reading classification
- On-device inference via TensorFlow Lite Micro, running directly on the ESP32 instead of in the cloud

## Author

**Athulya Sivadasan**
B.Tech Electronics and Communication Engineering, Ahalia School of Engineering and Technology
[LinkedIn](https://linkedin.com/in/athulya-sivadasan/) · [GitHub](https://github.com/Athulya-Sivadasan)
