#!/usr/bin/env python3
"""
get-screenshot-config.py - Extract screenshot configuration from test README.md

Reads YAML frontmatter if present, otherwise infers views from the
"JitterTrap Indicators" table in the README.

Usage: get-screenshot-config.py <readme-path>
Outputs: JSON config to stdout
"""

import sys
import re
import json

# Try to import yaml, fall back to regex parsing
try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False


# Mapping from metric keywords in README to view IDs
METRIC_TO_VIEW = {
    # TCP Window related
    'window': 'window',
    'zero-window': 'window',
    'zero window': 'window',
    'advertised window': 'window',
    'tcp window': 'window',
    'cwnd': 'window',
    'congestion': 'window',

    # RTT related
    'rtt': 'rtt',
    'round-trip': 'rtt',
    'round trip': 'rtt',
    'latency': 'rtt',

    # Throughput related
    'throughput': 'throughput',
    'bitrate': 'throughput',
    'bandwidth': 'throughput',
    'mbps': 'throughput',
    'kbps': 'throughput',

    # Top Talkers / Flow related
    'flow': 'toptalk',
    'top talker': 'toptalk',
    'connection': 'toptalk',

    # Histogram related
    'histogram': 'legend',
    'distribution': 'legend',
    'bimodal': 'legend',

    # Jitter related
    'jitter': 'legend',
    'ipg': 'legend',
    'inter-packet': 'legend',
    'packet gap': 'pgaps',

    # Packet related
    'packet size': 'legend',
    'frame size': 'legend',
    'packets/second': 'throughput',
    'pps': 'throughput',
}


def extract_frontmatter(content):
    """Extract YAML frontmatter from README content."""
    match = re.match(r'^---\s*\n(.*?)\n---', content, re.DOTALL)
    if not match:
        return None

    frontmatter_text = match.group(1)

    if HAS_YAML:
        try:
            return yaml.safe_load(frontmatter_text)
        except yaml.YAMLError:
            return None
    else:
        # Simple regex-based parsing for basic cases
        return parse_frontmatter_regex(frontmatter_text)


def parse_frontmatter_regex(text):
    """Fallback regex parser for YAML frontmatter when PyYAML not available."""
    config = {}

    # Look for views list
    views_match = re.search(r'views:\s*\[(.*?)\]', text)
    if views_match:
        views = [v.strip().strip('"\'') for v in views_match.group(1).split(',')]
        config['views'] = views

    # Look for data_accumulation_sec
    acc_match = re.search(r'data_accumulation_sec:\s*(\d+)', text)
    if acc_match:
        config['data_accumulation_sec'] = int(acc_match.group(1))

    return config if config else None


def infer_views_from_readme(content):
    """Infer screenshot views from JitterTrap Indicators table in README."""
    views = set()

    # Extract JitterTrap Indicators section
    indicators_match = re.search(
        r'## JitterTrap Indicators(.*?)(?=\n## |\Z)',
        content,
        re.DOTALL | re.IGNORECASE
    )

    if indicators_match:
        indicators_text = indicators_match.group(1).lower()
    else:
        # Fall back to searching entire document
        indicators_text = content.lower()

    # Check each metric keyword
    for keyword, view in METRIC_TO_VIEW.items():
        if keyword in indicators_text:
            views.add(view)

    # Always include toptalk as a baseline view
    views.add('toptalk')

    return list(views)


def get_default_config(views):
    """Generate default screenshot config with inferred views."""
    return {
        'views': views,
        'data_accumulation_sec': 10,  # Wait for time series to populate
        'triggers': [
            {
                'pattern': r'\[PASS\]',
                'delay_ms': 500,
                'views': views
            }
        ]
    }


def main():
    if len(sys.argv) != 2:
        print("Usage: get-screenshot-config.py <readme-path>", file=sys.stderr)
        sys.exit(1)

    readme_path = sys.argv[1]

    try:
        with open(readme_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except FileNotFoundError:
        print(f"Error: README not found: {readme_path}", file=sys.stderr)
        # Return minimal default config
        config = get_default_config(['toptalk', 'throughput'])
        print(json.dumps(config))
        sys.exit(0)
    except Exception as e:
        print(f"Error reading README: {e}", file=sys.stderr)
        sys.exit(1)

    # Try to get config from frontmatter first
    frontmatter = extract_frontmatter(content)
    if frontmatter and 'screenshot' in frontmatter:
        config = frontmatter['screenshot']
        # Ensure required fields exist
        if 'views' not in config:
            config['views'] = infer_views_from_readme(content)
        if 'data_accumulation_sec' not in config:
            config['data_accumulation_sec'] = 3
    else:
        # Infer from README content
        views = infer_views_from_readme(content)
        config = get_default_config(views)

    # Output JSON
    print(json.dumps(config))


if __name__ == '__main__':
    main()
