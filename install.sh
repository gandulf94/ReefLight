#!/bin/bash

sudo pip3 install -r reeflight_server/required-python-packages.txt
sudo cp reeflight_server/reeflight_server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl start reeflight_server.service
sudo systemctl enable reeflight_server.service
