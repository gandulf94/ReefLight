FROM python:3.7.4-slim

WORKDIR /reeflight
COPY . .
RUN pip install -r requirements.txt

CMD python reeflight.py
