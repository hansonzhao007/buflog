#/usr/bin/python3
import csv
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
from matplotlib.ticker import (MultipleLocator, FormatStrFormatter,
                               AutoMinorLocator)
from operator import truediv
from matplotlib.pyplot import figure
from matplotlib import gridspec
from matplotlib.ticker import FuncFormatter
from matplotlib.ticker import NullFormatter
from matplotlib import rcParams


# plt.rcParams["font.family"] = "serif"
plt.rcParams['axes.linewidth'] = 2
plt.rc('xtick', labelsize=20) 
plt.rc('ytick', labelsize=20) 
plt.rcParams['axes.labelsize'] = 20


indexes = ('fastfair', 'pactree', 'sptree', 'sptree_buf_log')


maxlat= {
    "load"   : 9000,   
    "read"   : 6000, 
    "readnon": 6000   
}

ytitles= {
    "fastfair"      : "Fast&Fair",
    "pactree"       : "Pactree",
    "sptree"        : "SPTree",
    "sptree_buf_log": "SPTree-B"
}


titles= {
    "load"      : "Insert",
    "read"      : "Positive Read",
    "readnon"   : "Negative Read",
}

fig = plt.figure(figsize=(16, 8)) 
gs = gridspec.GridSpec(4, 9) 
gs.update(wspace=0.2, hspace=0.0) # set the spacing between axes. 
axs  = [1,2,3,4,5,6,7,8,9,10,11,12]

for i in range(4):
    axs[i*3+0] = plt.subplot(gs[i:i+1, 0:3])
    axs[i*3+1] = plt.subplot(gs[i:i+1, 3:6])
    axs[i*3+2] = plt.subplot(gs[i:i+1, 6:9])
    

def xtickformat(x, pos):
        return '%1.0fus' % (x/1000)
formatter= FuncFormatter(xtickformat)


colorNoCaching="#FF7F7F"
colorCaching="#5E88C2"


ai=0
for index in indexes:
    df_lat_median = pd.read_csv("lat_median_" + index + ".parse", header=None)
    df_lat_median.columns= ['load', 'read', 'readnon']
    print(df_lat_median)

    for workloadtype in ["load", "read", "readnon"]:
        ax=axs[ai]
        filename = workloadtype.lower() + "_latency_" + index.lower() + ".parse"
        print(filename)
        df = pd.read_csv(filename, header=None)
        df.columns=["latency", "freq"]
        print(df)
        df.plot(x="latency", y="freq",ax=ax, style="o-",fillstyle='none')
        ax.set_xlim([1, maxlat[workloadtype.lower()]] )
        ax.fill_between(df.latency, df['freq'], 0, alpha=0.8, color=colorCaching)
        ax.xaxis.set_major_formatter(formatter)
        ax.set_yticklabels([])
        ax.get_legend().remove()
        ax.grid(axis = 'x', linewidth=1, linestyle='--')
        ax.set_axisbelow(True)
        ax.set_xlabel("")

        median_str = "%.2f" % (df_lat_median.iloc[0][workloadtype]/1000.0)
        ax.text(0.8, 0.76, "Median:\n" + median_str + "us",
            horizontalalignment='center',
            verticalalignment='center',
            transform = ax.transAxes, fontsize=20)

        if ai % 3 == 0:
            ax.set_ylabel(ytitles[index], fontsize=24)
        if (ai < 3):
            ax.set_title(titles[workloadtype], fontsize=28)
        if (ai < 9):
            ax.set_xticklabels([])
        ai=ai+1


fig.savefig("latency_compare.pdf",bbox_inches='tight', pad_inches=0.05)

