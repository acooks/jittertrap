
Goals:
  * to find top n <things>, by cumulative bytes over time, where <thing> is
  * src ip
  * dst ip
  * src port
  * dst port
  * ingress flow: {src_ip, src_port, dst_ip, dst_port} && src_ip is  ~local 
  * egress flow: {src_ip, src_port, dst_ip, dst_port} && src_ip is local


Definitions:
  Table: O(1) insert and O(1) lookup on average
  List:  O(1) append and O(1) remove at the head


Design:
=========

  Setup:
  * Create accumulation tables for 
      src_id, dst_ip, src_port, dst_port
      where each table entry contains
      { <src_ip|dst_ip|src_port|dst_port>, acc_bytes }

  Gather:
  * Append to a doubly-linked list of tuples containing
       { packet_len, src_ip, dst_ip, src_port, dst_port }
    O(1)
  * For each of m acc tables, accumulate the packet_len entry 
    O(m)

  Expire:
  * for each of list entry outside the window,
    * for each acc table, subtract the packet_len
    * remove table entry if acc == 0
    * delete list entry

  Calculate:
  * Sort acc table of interest
  * Retrieve top n table entries
