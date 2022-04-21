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


df = pd.read_csv("recovery.parse", header=None)
df.columns=["num", "time"]

def xtickformat(x, pos):
        return '%1.0fm' % (x/1000000)
formatter= FuncFormatter(xtickformat)

fig, ax = plt.subplots(figsize=(4, 2), sharex=True, squeeze=True)
df.plot(ax=ax, x="num", y="time", style="o-",fillstyle='none')

ax.xaxis.set_major_formatter(formatter)
ax.set_xlabel("Recovered Keys", fontsize=16)
ax.set_ylabel("Recovery Time(s)", fontsize=13)
ax.get_legend().remove()

fig.savefig("recovery.pdf",bbox_inches='tight', pad_inches=0.05)

