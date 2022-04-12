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

colors= {
    'cceh'        : '#F37F82',     
    'fastfair'    : '#3182BD',
    'skiplist'    : '#f7cd6b',
    'turbo30' : '#F37F82',
    'cceh30'  : '#7e72b5', 
    'clevel30': '#3182BD', 
    'clht30'  : '#808084'}


fig = plt.figure(figsize=(16, 10)) 
gs = gridspec.GridSpec(3, 9) 
gs.update(wspace=0.3, hspace=0.0) # set the spacing between axes. 
axs  = [1,2,3,4,5,6,7,8,9]

axs[0] = plt.subplot(gs[:1, 0:3])
axs[1] = plt.subplot(gs[:1, 3:6])
axs[2] = plt.subplot(gs[:1, 6:9])
axs[3] = plt.subplot(gs[1:2,  0:3])
axs[4] = plt.subplot(gs[1:2,  3:6])
axs[5] = plt.subplot(gs[1:2,  6:9])
axs[6] = plt.subplot(gs[2:3,  0:3])
axs[7] = plt.subplot(gs[2:3,  3:6])
axs[8] = plt.subplot(gs[2:3,  6:9])

def xtickformat(x, pos):
        return '%1.0fus' % (x/1000)
formatter= FuncFormatter(xtickformat)

i=0
for t in ["1", "20", "40"]:
    for index in indexes:
        ax=axs[i]
        filename = index + "_" + t + ".parse"
        df = pd.read_csv(filename, header=None)
        df.columns=["iops", "io_r", "io_w"]   
        df["io_r"] =  df["io_r"]/1024.0
        df["io_w"] =  df["io_w"]/1024.0
        print(df)
        df.plot.bar(ax=ax, y=["io_r", "io_w"])

        # plot io bars
        bars = ax.patches
        patterns =('//', '\\\\', '\\\\', '//', '..', 'xx', ' ')
        patterns_color = list(colors.values())
        hatches_color = [p for p in patterns_color for i in range(len(df))]
        hatches = [p for p in patterns for i in range(len(df))]
        bi=0
        for bar, hatch, color in zip(bars, hatches, hatches_color):
            bar.set_color(color)
            bar.set_edgecolor('k')
            bar.set_hatch(hatch)
            bi=bi+1
            
        # plot iops
        ax2 = ax.twinx()  
        df.plot(ax=ax2,y=["iops"], color='k', linewidth=2)

        # set yticklabels to inner
        xmin, xmax = ax.get_xlim()
        ax.set_xlim([-0.8, xmax])
        ymin, ymax = ax.get_ylim()
        ax.set_ylim([0.1, ymax*1.11])
        ymin, ymax = ax2.get_ylim()
        ax2.set_ylim([0.1, ymax*1.11])
        for label in ax.yaxis.get_ticklabels()[-2:]: # hidden last ticklabel
            label.set_visible(False)
        for label in ax2.yaxis.get_ticklabels()[-2:]: # hidden last ticklabel
            label.set_visible(False)
        ax.tick_params(axis="y", direction="inout", pad=-4)
        for label in ax.yaxis.get_ticklabels():
            label.set_horizontalalignment('left')
        ax2.tick_params(axis="y", direction="inout", pad=-4)
        for label in ax2.yaxis.get_ticklabels():
            label.set_horizontalalignment('right')
        
        # set units
        ax.text(0.05, 0.92, " GB",
            horizontalalignment='center',
            verticalalignment='center',
            transform = ax.transAxes, fontsize=20)
        ax2.text(0.88, 0.93, " Mops/s",
            horizontalalignment='center',
            verticalalignment='center',
            transform = ax.transAxes, fontsize=16)
        

        ax.get_legend().remove()
        ax2.get_legend().remove()
        # plot legend
        if i == 1:            
            ax.legend(["IO Read", "IO Write"], loc="upper left", ncol=2, fontsize=26, edgecolor='k',facecolor='w', framealpha=0, mode="expand", bbox_to_anchor=(-0.9, 1.45, 1.8, 0))            
            ax2.legend(["Throughput"], loc="center right", fontsize=26, edgecolor='k',facecolor='w', framealpha=0, mode="expand", bbox_to_anchor=(1, 1.26, 1, 0))

        

        # set titles
        if i < 3:
            ax.set_title(index, fontsize=26)
        if i == 0:
            ax.set_ylabel("1 Thread", fontsize=26)
        if i == 3:
            ax.set_ylabel("20 Threads", fontsize=26)
        if i == 6:
            ax.set_ylabel("40 Threads", fontsize=26)

        if i >= 6:
            if (len(df) == 4):
                ax.set_xticklabels([index, index+"-B", index+"-BO", index+"-BOC"])
            else:
                ax.set_xticklabels([index, index+"-BO", index+"-BOC"])
            
        ax.tick_params(axis='x', labelsize=26, rotation=45)
        i=i+1

fig.savefig("case_study_buf.pdf",bbox_inches='tight', pad_inches=0.05)

