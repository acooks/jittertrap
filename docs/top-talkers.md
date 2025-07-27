# Jittertrap Top Talkers Feature Overview

This document describes the architecture and data flow for the "Top Talkers" feature, which performs real-time network traffic analysis and visualization.

## Architecture

The system is a two-part application:

    A C-based backend responsible for high-performance packet capture, flow tracking, and data aggregation.

    A Javascript-based frontend responsible for data processing, final summarization, and interactive visualization.

This decoupled design allows the backend to focus on efficient, real-time processing while giving the frontend the flexibility to handle data presentation.

## Backend (C Language)

The backend runs as a set of high-priority threads to ensure timely packet capture and processing.

### 1. Packet Capture and Flow Tracking (intervals.c)

    Capture: The pcap library is used to capture raw packets from a selected network interface in real-time. The capture process is managed within a dedicated thread (tt_intervals_run) to minimize packet loss.

    Decoding: Each captured packet is passed through a series of decoders (decode.c) to parse Ethernet, IP (v4/v6), and Transport layer (TCP, UDP, etc.) headers. The relevant data (IPs, ports, protocol, byte count) is extracted into a flow_record struct.

    Flow Tracking: The system uses hash tables (uthash.h) to track network flows.

        Interval Tables: Multiple hash tables (incomplete_flow_tables, complete_flow_tables) track flow statistics over different short-term intervals (e.g., 5ms, 100ms, 1s) defined in intervals_user.c.

        Reference Table: A primary hash table (flow_ref_table) tracks flows over a longer, sliding time window. This table is used to identify the overall top flows across all intervals.

### 2. Data Forwarding (tt_thread.c)

    Data Preparation: A separate thread (intervals_run) periodically wakes up to process the tracked flow data. It identifies the top flows from the reference table.

    Candidate Pool: The m2m function prepares a message for the frontend. Crucially, it creates a "candidate pool" of top flows. It sends up to MAX_FLOWS (defined as 20 in jt_msg_toptalk.h) of the highest-volume flows. This provides the frontend with more context than just the top 10.

    Message Queue: The prepared message is sent to the frontend via a message queue system (mq_tt_produce).

## Frontend (Javascript)

The frontend receives the candidate pool of flows and performs the final processing and visualization.

### 1. Data Ingestion & Processing (jittertrap-core.js)

    Websocket: The frontend receives messages from the backend via a websocket connection (jittertrap-websocket.js).

    Core Processing: The processTopTalkMsg function in jittertrap-core.js is the main entry point for new data. It takes the incoming message containing up to 20 flows and updates its internal time-series data structures (flowsTS, flowRank, flowsTotals). flowRank is a sorted list of all flows received within the current time window, ranked by total bytes.

### 2. Visualization and Aggregation (jittertrap-chart-toptalk.js)

    Final Summarization: This is the key to the new architecture. The chart's redraw function calls a helper, processAndAggregateChartData.

        This function takes the full list of flows prepared by jittertrap-core.js.

        It defines a LEGEND_DISPLAY_LIMIT (set to 10).

        It splits the incoming data: the first 10 flows are kept as individual series, and the rest (flows 11-20) are aggregated.

        It sums the byte counts of the remaining flows for each timestamp into a new, special data series with the key 'other'.

    Rendering: The final dataset, consisting of the top 10 individual flows plus the single "Other" aggregate flow, is passed to the D3.js charting library. The chart's existing logic recognizes the 'other' key and assigns it a unique color and a simple "Other Flows" label in the legend, displaying it as part of the stacked area chart.
