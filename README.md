# SOL
Long-term solar intensity sensing

People considering a photovoltaic installation on their property should be able to make a data-driven decision about the viability of such a system. While projects such as Google's Project Sunroof can provide large-scale estimates of solar intensity from satellite images, SOL will be able to provide data that incorporates local effects such as roof slope and direction, tree cover, or obstruction from nearby buildings.

See project logs for SOL at the Hackaday project page: https://hackaday.io/project/158984-sol-long-term-solar-intensity-sensing

R2 included a variety of upgrades to lower cost and improve performance:

- ADC with PGA allows better measurement accuracy (ESP32 ADC is highly nonlinear)
- Temperature sensor added (ESP32 internal temperature sensor has very poor performance)
- RTC added for accurate time monitoring without connecting to network
- Smaller PCB with smaller battery (14500 battery)
- Fixed dimensions and location of mounting hole (V1 didn't fit well in case)
- Easier programming by moving required pins to pin headers

However, R2 had some issues:

- Mistake on trace routing held temperature sensor output to ground, making it not work
- Forgot to add load capacitors on RTC crystal, so RTC does not work
- PCB vias not covered with soldermask underneath battery connectors, which would cause shorts
- The charger IC has a large pad underneath it, which was not included in its Eagle part
- The battery silkscreen was left on top of the PCB, when it should have been on the bottom. Not a functional issue, since battery connectors can be attached through either side
- Decoupling capacitor was not put near the ADC, when it should have been. The ADC still seems to work fine, but this should still be fixed
