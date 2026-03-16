#!/bin/bash
# Fix Mosquitto to allow external connections
# Run on Jetson: sudo bash ~/Desktop/product/202603-ai-robot-esp32/docs/fix-mosquitto.sh

echo "listener 1883 0.0.0.0" > /etc/mosquitto/conf.d/external.conf
echo "allow_anonymous true" >> /etc/mosquitto/conf.d/external.conf

echo "Config written:"
cat /etc/mosquitto/conf.d/external.conf

systemctl restart mosquitto
echo "Mosquitto restarted"
systemctl is-active mosquitto
