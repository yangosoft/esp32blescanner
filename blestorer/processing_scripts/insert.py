#!/usr/bin/env python3
"""Insert MAC scan lines from a text file into dbmacs.sqlite3.

File format: first line is timestamp (ISO), then one JSON object per line:
2026-04-13T03:27:20.053Z
{"mac":"de:99:85:06:42:7b","rssi":-92,"name":"Govee"}

Usage: insert.py [input-file] [--db DBPATH]
"""

from __future__ import annotations
import argparse
import json
import os
import sqlite3
import sys
from typing import List, Tuple, Optional


def parse_args():
    p = argparse.ArgumentParser(description='Insert MAC scan file into SQLite DB')
    p.add_argument('file', nargs='?', default='macinsert.txt', help='Input file to parse')
    p.add_argument('--db', default='dbmacs.sqlite3', help='SQLite database file')
    return p.parse_args()


def load_rows(path: str) -> Tuple[str, List[Tuple[str, str, str, Optional[int]]]]:
    rows = []
    with open(path, 'r', encoding='utf-8') as fh:
        ts = fh.readline().strip()
        if not ts:
            raise SystemExit('Missing timestamp on first line')
        for ln in fh:
            ln = ln.strip()
            if not ln:
                continue
            try:
                obj = json.loads(ln)
            except json.JSONDecodeError:
                print('Warning: skipping invalid JSON line:', ln, file=sys.stderr)
                continue
            mac = obj.get('mac', '')
            name = obj.get('name', '')
            rssi = obj.get('rssi')
            try:
                rssi = None if rssi is None else int(rssi)
            except Exception:
                rssi = None
            rows.append((ts, mac, name, rssi))
    return ts, rows


def ensure_table(conn: sqlite3.Connection) -> None:
    conn.execute('''CREATE TABLE IF NOT EXISTS macs(
        m_time TEXT,
        m_mac TEXT,
        m_name TEXT,
        m_rssi INTEGER,
        PRIMARY KEY(m_time, m_mac, m_name)
    )''')


def insert_rows(conn: sqlite3.Connection, rows: List[Tuple[str, str, str, Optional[int]]]) -> int:
    if not rows:
        return 0
    cur = conn.cursor()
    cur.executemany('INSERT OR REPLACE INTO macs(m_time,m_mac,m_name,m_rssi) VALUES (?,?,?,?)', rows)
    conn.commit()
    return cur.rowcount


def main():
    args = parse_args()
    if not os.path.exists(args.file):
        print(f"Input file '{args.file}' not found", file=sys.stderr)
        raise SystemExit(2)
    try:
        ts, rows = load_rows(args.file)
    except SystemExit as e:
        print(str(e), file=sys.stderr)
        raise

    conn = sqlite3.connect(args.db)
    try:
        ensure_table(conn)
        inserted = insert_rows(conn, rows)
    finally:
        conn.close()

    if inserted:
        print(f'Inserted/updated {inserted} rows into {args.db}')
    else:
        print('No rows inserted')


if __name__ == '__main__':
    main()
