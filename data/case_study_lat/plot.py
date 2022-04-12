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


indexes = ('cceh', 'fastfair', 'skiplist')

maxlat= {
    'cceh_load'        : 9000,     
    'fastfair_load'    : 18000,
    'skiplist_load'    : 36000,
    'cceh_read'        : 1900,     
    'fastfair_read'    : 5900,
    'skiplist_read'    : 26000,
}

fig = plt.figure(figsize=(16, 10)) 
gs = gridspec.GridSpec(2, 9) 
gs.update(wspace=0.3, hspace=0.1) # set the spacing between axes. 
axs  = [1,2,3,4,5,6]

axs[0] = plt.subplot(gs[:1, 0:3])
axs[1] = plt.subplot(gs[:1, 3:6])
axs[2] = plt.subplot(gs[:1, 6:9])
axs[3] = plt.subplot(gs[1:, 0:3])
axs[4] = plt.subplot(gs[1:, 3:6])
axs[5] = plt.subplot(gs[1:,  6:9])

def xtickformat(x, pos):
        return '%1.0fus' % (x/1000)
formatter= FuncFormatter(xtickformat)


ai=0
for name in indexes:

    

    ax=axs[ai]
    filename = "load_lat_" + name + ".parse"
    print(filename)
    df = pd.read_csv(filename, header=None)
    df.columns=["latency", "freq"]
    df.plot(x="latency", y="freq", ax=ax, alpha=0.5, color="#5E88C2")
    ax.set_xlim([0, maxlat[name + "_load"]])
    ax.fill_between(df.latency, df['freq'], 0, alpha=0.5, color="#5E88C2")
    ax.xaxis.set_major_formatter(formatter)
    ax.set_yticklabels([])
    ax.legend()
    ax.set_title(name, fontsize=28)
    ax.set_xlabel("")
    if ai == 0:
        ax.set_ylabel("Insert Latency Distribution")
    
    
    ax=axs[ai+3]
    filename = "read_lat_" + name + ".parse"
    print(filename)
    df = pd.read_csv(filename, header=None)
    df.columns=["latency", "freq"]
    df.plot(x="latency", y="freq", ax=ax, alpha=0.5, color="#5E88C2")
    ax.set_xlim([0, maxlat[name+ "_read"]])
    ax.fill_between(df.latency, df['freq'], 0, alpha=0.5, color="#5E88C2")
    ax.xaxis.set_major_formatter(formatter)
    ax.set_yticklabels([])
    ax.legend()
    ax.set_xlabel("")
    if ai == 0:
        ax.set_ylabel("Read Latency Distribution")
    ax.set_xlabel("")
    ai=ai+1

ai=0
for name in indexes:
    ax=axs[ai]
    filename = "load_lat_" + name + "_spoton_di.parse"
    print(filename)
    df = pd.read_csv(filename, header=None)
    df.columns=["latency", "freq"]
    df.plot(x="latency", y="freq", ax=ax, alpha=0.5, color='r')
    ax.set_xlim([0, maxlat[name+ "_load"]])
    ax.fill_between(df.latency, df['freq'], 0, alpha=0.5, color='r')
    ax.set_yticklabels([])
    ax.legend([])

    ax=axs[ai+3]
    filename = "read_lat_" + name + "_spoton_di.parse"
    print(filename)
    df = pd.read_csv(filename, header=None)
    df.columns=["latency", "freq"]
    df.plot(x="latency", y="freq", ax=ax, alpha=0.5, color='r')
    ax.set_xlim([0, maxlat[name + "_read"]])
    ax.fill_between(df.latency, df['freq'], 0, alpha=0.5, color='r')
    ax.set_yticklabels([])
    ax.legend([])
    ax.set_xlabel("")

    ai=ai+1
    


fig.savefig("case_study_lat_with_caching.pdf",bbox_inches='tight', pad_inches=0.05)

