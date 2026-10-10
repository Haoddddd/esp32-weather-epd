#!/usr/bin/env python3
"""Fetch the list of Chinese prefecture-level divisions from Wikidata.

Writes cities_zh_CN.csv (committed, so builds do not need network access)
with one row per prefecture-level division plus the municipalities and
special administrative regions:

  province, name, short_name, lat, lon, wikidata_id

Coordinates are those of the division's seat of government (or the seat's own
seat, when the seat is a county) when Wikidata has them, otherwise those of the
division itself. Wikidata content is CC0.

Usage:
  python3 fetch_cities.py
"""

import csv
import json
import math
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
OUT_CSV = SCRIPT_DIR / 'cities_zh_CN.csv'

ENDPOINT = 'https://query.wikidata.org/sparql'
USER_AGENT = 'esp32-weather-epd-city-list/0.1'

# Wikidata classes of prefecture-level divisions
TYPES = {
    'Q748149': 'prefecture-level city',
    'Q788104': 'autonomous prefecture',
    'Q1045608': 'prefecture',
    'Q288653': 'league',
}

QUERY = '''
SELECT ?item ?zh ?provzh ?coord ?seatcoord ?townCoord WHERE {
  VALUES ?type { %s }
  # any non-deprecated type statement, not only the preferred one (e.g.
  # Harbin's preferred type is sub-provincial city)
  ?item p:P31 [ ps:P31 ?type ; wikibase:rank ?rank ] .
  FILTER(?rank != wikibase:DeprecatedRank)
  FILTER NOT EXISTS { ?item wdt:P576 ?dissolved }
  FILTER NOT EXISTS { ?item wdt:P31 wd:Q21286738 }  # permanent duplicate
  OPTIONAL { ?item rdfs:label ?zhcn . FILTER(LANG(?zhcn) = "zh-cn") }
  OPTIONAL { ?item rdfs:label ?zhhans . FILTER(LANG(?zhhans) = "zh-hans") }
  OPTIONAL { ?item rdfs:label ?zhany . FILTER(LANG(?zhany) = "zh") }
  BIND(COALESCE(?zhcn, ?zhhans, ?zhany) AS ?zh)
  FILTER(BOUND(?zh))
  OPTIONAL { ?item wdt:P625 ?coord }
  OPTIONAL {
    ?item wdt:P36 ?seat .
    OPTIONAL { ?seat wdt:P625 ?seatcoord }
    # the seat of a prefecture is a county, which can be large; prefer the
    # county's own seat (a town) when known
    OPTIONAL { ?seat wdt:P36 ?town . ?town wdt:P625 ?townCoord }
  }
  OPTIONAL { ?item wdt:P131 ?prov . ?prov rdfs:label ?provzh .
             FILTER(LANG(?provzh) = "zh-cn") }
}
''' % ' '.join('wd:' + t for t in TYPES)

# Divisions whose Wikidata parent is not their province.
PROVINCE_OVERRIDES = {
    '塔城地区': '新疆维吾尔自治区',
    '阿勒泰地区': '新疆维吾尔自治区',
}

# Stale Wikidata items for divisions that have been converted or merged.
EXCLUDED_ITEMS = {
    'Q21418817',  # 吐鲁番地区, became 吐鲁番市 in 2015
    'Q28798559',  # 海东地区, became 海东市 in 2013
}

# Coordinates fixed by hand where Wikidata's seat coordinates are off.
# {wikidata_id: (lat, lon)}
# These divisions have no seat on Wikidata and two conflicting coordinates;
# use the one at the seat of government.
COORD_OVERRIDES = {
    'Q1619433': (46.0696, 122.0658),  # 兴安盟, seat 乌兰浩特
    'Q508594': (36.9580, 100.8998),   # 海北州, seat 西海镇
    'Q449963': (38.8474, 105.7031),   # 阿拉善盟, seat 巴彦浩特
    'Q1001390': (26.2706, 107.5123),  # 黔南州, seat 都匀
}

# Display order of province groups (municipalities first, then GB/T 2260).
PROVINCE_ORDER = [
    '直辖市', '河北省', '山西省', '内蒙古自治区', '辽宁省', '吉林省', '黑龙江省',
    '江苏省', '浙江省', '安徽省', '福建省', '江西省', '山东省', '河南省', '湖北省',
    '湖南省', '广东省', '广西壮族自治区', '海南省', '四川省', '贵州省', '云南省',
    '西藏自治区', '陕西省', '甘肃省', '青海省', '宁夏回族自治区', '新疆维吾尔自治区',
    '特别行政区',
]

# Province-level divisions without prefecture-level subdivisions.
# (province, name, short_name, lat, lon, wikidata_id)
EXTRA_ROWS = [
    ('直辖市', '北京市', '北京', 39.9042, 116.4074, 'Q956'),
    ('直辖市', '天津市', '天津', 39.1467, 117.2056, 'Q11736'),
    ('直辖市', '上海市', '上海', 31.2325, 121.4692, 'Q8686'),
    ('直辖市', '重庆市', '重庆', 29.5500, 106.5069, 'Q11725'),
    ('特别行政区', '香港特别行政区', '香港', 22.2783, 114.1747, 'Q8646'),
    ('特别行政区', '澳门特别行政区', '澳门', 22.1667, 113.5500, 'Q14773'),
]

# Ethnic group names that appear in autonomous prefecture names, used to
# derive the conventional short name (e.g. 延边朝鲜族自治州 -> 延边州).
ETHNIC_GROUPS = ['朝鲜', '土家', '苗', '藏', '羌', '彝', '布依', '侗', '傣',
                 '景颇', '哈尼', '白', '傈僳', '回', '蒙古', '哈萨克', '柯尔克孜',
                 '壮']
AUTONOMOUS_PREFECTURE = re.compile(
    r'^(.+?)(?:(?:%s)族?)+自治州$' % '|'.join(ETHNIC_GROUPS))


def short_name(name):
    match = AUTONOMOUS_PREFECTURE.match(name)
    if match:
        return match.group(1) + '州'
    for suffix in ('市', '地区'):
        if name.endswith(suffix) and len(name) > len(suffix) + 1:
            return name[:-len(suffix)]
    return name


def distance_km(a, b):
    lat1, lon1, lat2, lon2 = map(math.radians, (*a, *b))
    cos_angle = (math.sin(lat1) * math.sin(lat2)
                 + math.cos(lat1) * math.cos(lat2) * math.cos(lon1 - lon2))
    return 6371 * math.acos(max(-1.0, min(1.0, cos_angle)))


def parse_point(value):
    lon, lat = value[len('Point('):-1].split()
    return round(float(lat), 4), round(float(lon), 4)


def main():
    url = ENDPOINT + '?' + urllib.parse.urlencode({'query': QUERY})
    request = urllib.request.Request(url, headers={
        'Accept': 'application/sparql-results+json',
        'User-Agent': USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as resp:
        bindings = json.load(resp)['results']['bindings']

    items = {}
    for b in bindings:
        qid = b['item']['value'].rsplit('/', 1)[1]
        item = items.setdefault(qid, {'name': b['zh']['value'],
                                      'prov': set(), 'coord': set(),
                                      'seat': set(), 'town': set()})
        if 'provzh' in b:
            item['prov'].add(b['provzh']['value'])
        if 'coord' in b:
            item['coord'].add(b['coord']['value'])
        if 'seatcoord' in b:
            item['seat'].add(b['seatcoord']['value'])
        if 'townCoord' in b:
            item['town'].add(b['townCoord']['value'])

    rows = []
    problems = []
    for qid, item in items.items():
        if qid in EXCLUDED_ITEMS:
            continue
        name = item['name']
        province = PROVINCE_OVERRIDES.get(name)
        if province is None:
            provinces = item['prov'] - {'中华人民共和国'}
            if len(provinces) != 1:
                problems.append(f'{qid} {name}: provinces {provinces}')
                continue
            province = provinces.pop()
        points = (sorted(item['town']) or sorted(item['seat'])
                  or sorted(item['coord']))
        if not points:
            problems.append(f'{qid} {name}: no coordinates')
            continue
        if qid in COORD_OVERRIDES:
            lat, lon = COORD_OVERRIDES[qid]
        else:
            lat, lon = parse_point(points[0])
            own = [parse_point(p) for p in item['coord']]
            if own and min(distance_km((lat, lon), o) for o in own) > 50:
                problems.append(f'{qid} {name}: seat ({lat}, {lon}) is far '
                                f'from the division\'s own coordinates {own}')
        rows.append((province, name, short_name(name), lat, lon, qid))

    rows.extend(EXTRA_ROWS)
    unknown = {r[0] for r in rows} - set(PROVINCE_ORDER)
    if unknown:
        sys.exit(f'error: provinces missing from PROVINCE_ORDER: {unknown}')
    # GB18030 byte order sorts common characters by pinyin
    rows.sort(key=lambda r: (PROVINCE_ORDER.index(r[0]),
                             r[1].encode('gb18030')))
    names = [r[1] for r in rows]
    duplicates = {n for n in names if names.count(n) > 1}
    if duplicates:
        sys.exit(f'error: duplicate names: {duplicates}')

    with OUT_CSV.open('w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['province', 'name', 'short_name', 'lat', 'lon',
                         'wikidata_id'])
        writer.writerows(rows)

    for p in problems:
        print('check:', p)
    print(f'wrote {len(rows)} rows to {OUT_CSV.name}')


if __name__ == '__main__':
    main()
