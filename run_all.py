#!/bin/python3

import subprocess
import re

delay_file = "./results/_delay.csv"
pkts_per_second_list = range(1, 21)

open(delay_file, 'w').close()

for pkts_per_second in pkts_per_second_list:
    gen_interval = str(1.0 / pkts_per_second)
    subprocess.run(["./run_test", gen_interval])
    with open("./results/General-#0.sca", "r") as sca_file:
        avg_delay = re.search("scalar Network.nodeRx.sink AvgDelay (.*)", sca_file.read()).group(1)
        subprocess.getoutput('echo "{},{}" >> {}'.format(pkts_per_second, avg_delay, delay_file))
