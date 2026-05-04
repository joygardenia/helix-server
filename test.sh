#!/usr/bin/env zsh
wrk -t4 -c100 -d10s --latency http://127.0.0.1:8080/
wrk -t4 -c500 -d10s --latency http://127.0.0.1:8080/
wrk -t4 -d1000 -d10s --latency http://127.0.0.1:8080/
