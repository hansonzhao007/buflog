#/usr/bin/python3
import csv
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FuncFormatter
from matplotlib.ticker import (MultipleLocator, FormatStrFormatter,
                               AutoMinorLocator)

plt.rcParams["font.family"] = "serif"
plt.rcParams['axes.linewidth'] = 1.2

# read data
markers= ['', 'o']
files = ['seq', 'rnd']
colors = ['#2A5D97', '#8EA830']
dashes= {
    'wa' : [2, 2],
    'th' : [2, 0]
}


def Plot(ylable, is_legend):
    # Plot Average Log Distance
    fig, ax = plt.subplots(figsize=(4, 3.6))
    ax_twin = ax.twinx()

    ax.set_ylim([0, 30])
    # ax_twin.set_ylim([0.6, 4.4])
    ax.set_xlim([0, 512])

    legends = []
    for i in (0, 1):
        filename = files[i] + ".csv"
        df = pd.read_csv(filename)        
        df.set_index('bucket_size', inplace=True)
        print(df)
        df['wa'] = df['pm_w'] / (10*1000000/1024)

        df['throughput'].plot(
            ax=ax, 
            linewidth=2,
            kind='line', 
            dashes=dashes['th'], 
            marker=markers[i],
            markersize=10,
            color=colors[i],
            fillstyle='none')

        df['wa'].plot(
            ax=ax_twin,
            linewidth=2,
            kind='line', 
            dashes=dashes['wa'], 
            marker=markers[i],
            markersize=10,
            color=colors[i], 
            fillstyle='none')
            
    h1, l1 = ax.get_legend_handles_labels()
    h2, l2 = ax_twin.get_legend_handles_labels()
    ax.legend(h1+h2, 
        ["seq_throughput", "rnd_throughput", "seq_wa", "rnd_wa"], 
        loc=3, ncol=2,
        framealpha=0,
        bbox_to_anchor=(-0.01, -0.03, 1.03, 0.1)
        )


    # if (is_legend):
    #     ax.legend([64, 32, 16, 8, 4, 2], fontsize=11)

    plt.xticks(np.arange(0, 513, 64), rotation='vertical')

    ax.tick_params(axis='x', labelsize=12, rotation=45)
    ax.tick_params(axis='y', labelsize=12)
    ax_twin.tick_params(axis='y', labelsize=12)

    ax.set_ylabel("Throughput (Mops/s)", fontsize=14, color='k')
    ax_twin.set_ylabel("Write Amplification", fontsize=14, color='k')
    ax.set_xlabel("Bucket Size (Byte)", fontsize=14)

    
    # ax.set_xticklabels(df.bucket_size, rotation=0)
    
    # ax.yaxis.grid(linewidth=1, linestyle='--')
    plt.savefig("res.pdf", bbox_inches='tight', pad_inches=0.05)


Plot("Load Factor", False)