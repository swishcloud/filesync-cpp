#!/bin/sh
set -e
development="true" build/filesync/filesync  listen --listen_port 8008 --files_path ~/sync_files