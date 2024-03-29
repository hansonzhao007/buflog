#/usr/bin/python3
import csv
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FuncFormatter
from matplotlib.ticker import (MultipleLocator, FormatStrFormatter,
                               AutoMinorLocator)
import matplotlib.ticker as plticker

plt.rcParams["font.family"] = "serif"
plt.rcParams['axes.linewidth'] = 1.2

# read data
markers= ['o', 's', '|']
files = ['seq_batch', 'rnd', 'buffer']

colors = ['black', 'blue', 'red']
dashes= {
    'wa' : [2, 2],
    'th' : [4, 0]
}


def Plot(ylable, is_legend):
    # Plot Average Log Distance
    fig, ax = plt.subplots(figsize=(4, 3.6))
    ax_twin = ax.twinx()

    fig2, ax2 = plt.subplots(figsize=(4, 3.6))

    for i in [0,1,2]:
        filename = files[i] + ".csv"
        df = pd.read_csv(filename)   
        df.set_index('bucket_size', inplace=True)
        
        print(i, filename)
        print(df)

        df['wa2'] = df['pm_w'] / (10*1000000/1024/1024*16)
        df['throughput'].plot(
            ax=ax, 
            linewidth=2,
            kind='line', 
            dashes=dashes['th'], 
            marker=markers[i],
            markersize=10,
            color=colors[i],
            fillstyle='none')
        df['wa2'].plot(
            ax=ax_twin,
            linewidth=2,
            kind='line', 
            dashes=dashes['wa'], 
            marker=markers[i],
            markersize=10,
            color=colors[i],
            fillstyle='none')

        (df['pm_bw_w'] + df['pm_bw_r']).plot(
            ax=ax2,
            linewidth=2,
            kind='line', 
            marker=markers[i],
            markersize=10,
            color=colors[i],
            fillstyle='none'
        )

    h1, l1 = ax.get_legend_handles_labels()
    h2, l2 = ax_twin.get_legend_handles_labels()
    ax.legend(h1+h2, 
        ["seq_throughput", "rnd_throughput", "buffer_throughput",
           "seq_wa", "rnd_wa", "buffer_wa"],
        loc="upper left", ncol=2,
        framealpha=0,
        bbox_to_anchor=(-0.05, 1.15, 1.03, 0.1)
    )   

    ax_twin.axhline(1, linestyle='--', color ='grey', lw=1) # lines

    # throughput
    ax.set_ylim([0.01, 29])    
    loc2 = plticker.MultipleLocator(base=5) # this locator puts ticks at regular intervals
    ax.yaxis.set_major_locator(loc2)
    ax.set_xticks(np.arange(0, 575, 64), rotation='vertical')

    # wa
    ax_twin.set_ylim([0.01, 23])
    loc = plticker.MultipleLocator(base=4.0) # this locator puts ticks at regular intervals
    # ax_twin.set_xticks(list(ax_twin.get_xticks()) + [1])    
    ax_twin.yaxis.set_major_locator(loc)
    
    ax_twin.text(1.05, 0.05, '1', fontsize=12, horizontalalignment='center', verticalalignment='center', transform=ax.transAxes)

    ax.tick_params(axis='x', labelsize=12, rotation=45)
    ax.tick_params(axis='y', labelsize=12)
    ax_twin.tick_params(axis='y', labelsize=12)
    ax.set_ylabel("Throughput (Mops/s)", fontsize=14, color='k')
    ax_twin.set_ylabel("Write Amplification", fontsize=14, color='k')
    ax.set_xlabel("Bucket Size (Byte)", fontsize=14)
    ax.yaxis.grid(linewidth=0.5, linestyle='--')
    

    fig.savefig("wa_throughput.pdf", bbox_inches='tight', pad_inches=0.05)
    fig2.savefig("pmem_bw.pdf", bbox_inches='tight', pad_inches=0.05)

Plot("Load Factor", False)