# AM synchrone : deux bandes contre une seule bande, avec une station voisine (detection lineaire :
# on mesure separement ce que devient le signal voulu et ce que deviennent voisin + bruit)
import numpy as np
from scipy import signal
fs=48000; T=20; n=fs*T; rng=np.random.default_rng(1)
b0,a0=signal.butter(4,[300,3000],'band',fs=fs)
def parole():
    x=rng.standard_normal(n); x=signal.lfilter(b0,a0,x); return x/np.std(x)*0.3
t=np.arange(n)/fs
m1=parole(); m2=parole()
bl,al=signal.butter(6,4500,fs=fs)
h=signal.firwin(801,2500,fs=fs)*np.exp(2j*np.pi*(-2000)*np.arange(801)/fs)   # garde -4,5..+0,5 kHz
def dsb(z): return signal.lfilter(b0,a0,np.real(signal.lfilter(bl,al,z)))
def lsb(z): return signal.lfilter(b0,a0,2*np.real(signal.lfilter(h,1,z)))
def snr(f,utile,parasite):
    u=f(utile)[4000:]; p=f(parasite)[4000:]; return 10*np.log10(np.dot(u,u)/np.dot(p,p))
voulu=1+m1
bruit=0.05*(rng.standard_normal(n)+1j*rng.standard_normal(n))
print(f"sans voisin : deux bandes {snr(dsb,voulu,bruit):5.1f} dB, une bande {snr(lsb,voulu,bruit):5.1f} dB")
for off,niv in [(5000,1.0),(5000,0.3),(6000,1.0),(7000,1.0),(9000,1.0)]:
    v=niv*(1+m2)*np.exp(2j*np.pi*off*t)
    print(f"voisin +{off/1000:.0f} kHz {20*np.log10(niv):+3.0f} dB : deux bandes {snr(dsb,voulu,v+bruit):5.1f} dB, une bande (cote libre) {snr(lsb,voulu,v+bruit):5.1f} dB")
