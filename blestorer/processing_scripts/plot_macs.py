#!/usr/bin/env python3
"""Create a heatmap of unique MAC counts per weekday and 30-minute interval.

Usage:
  ./plot_macs.py --db dbmacs.sqlite3 --out macs_heatmap.png

Outputs a PNG heatmap (7 rows = Mon..Sun, 48 columns = 30-min intervals).
"""
from __future__ import annotations
import argparse
import sqlite3
import sys

def parse_args():
    p = argparse.ArgumentParser(description='Plot MAC counts per weekday and 30-min interval')
    p.add_argument('--db', default='dbmacs.sqlite3', help='SQLite DB file')
    p.add_argument('--out', default='macs_heatmap.png', help='Output image file')
    return p.parse_args()


def fetch_counts(db_path: str):
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    sql = '''
    WITH p AS (
            SELECT lower(trim(m_mac)) AS m_mac,
                         replace(replace(m_time,'T',' '),'Z','') AS t
            FROM macs
        )
        SELECT
            ((CAST(strftime('%w', t) AS integer) + 6) % 7) AS dow,
            (CAST(strftime('%H', t) AS integer) * 2
                 + CASE WHEN CAST(strftime('%M', t) AS integer) >= 30 THEN 1 ELSE 0 END) AS interval,
            COUNT(DISTINCT m_mac) AS cnt
        FROM p
        GROUP BY dow, interval
        ORDER BY dow, interval
    '''
    cur.execute(sql)
    rows = cur.fetchall()
    conn.close()
    return rows


def build_matrix(rows):
    import numpy as np

    mat = np.zeros((7, 48), dtype=int)
    for dow, interval, cnt in rows:
        if 0 <= dow < 7 and 0 <= interval < 48:
            mat[dow, interval] = cnt
    return mat


def plot_matrix(mat, out_path):
    try:
        import matplotlib.pyplot as plt
    except Exception:
        print('matplotlib is required: pip install matplotlib', file=sys.stderr)
        raise

    weekdays = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']

    fig, ax = plt.subplots(figsize=(14, 4))
    im = ax.imshow(mat, aspect='auto', cmap='viridis', origin='lower')

    ax.set_yticks(range(7))
    ax.set_yticklabels(weekdays)

    xticks = list(range(0, 48, 2))
    xlabels = [f"{i//2:02d}:00" for i in xticks]
    ax.set_xticks(xticks)
    ax.set_xticklabels(xlabels, rotation=45)

    ax.set_xlabel('Time (30-min intervals)')
    ax.set_ylabel('Weekday')
    ax.set_title('Unique MAC count per weekday / 30-minute interval')

    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label('Unique MACs')

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved heatmap to {out_path}')


def main():
    args = parse_args()
    rows = fetch_counts(args.db)
    mat = build_matrix(rows)
    plot_matrix(mat, args.out)


if __name__ == '__main__':
    main()
