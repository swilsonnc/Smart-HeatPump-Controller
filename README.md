# Smart-HeatPump-Controller
Makes a dumb temp/time defrost heatpump smarter.

The Problem I am trying to solve:

Some heatpumps are not very smart when it comes to initiating defrost.  These heat pumps consist of a thermo switch connected directly to the coil of the heat pump and a defrost board that has a selectable timer via a jumper.
When the coil temp falls below 32F the thermo switch closes which allows 24VAC to flow through it to the timer.  This starts the timer and it starts counting run time.  If the timer is set to 90 minutes then the timer will be up after 90 minutes of run time (idle time doesn't count).  One the timer is up the defrost board will initiate defrost which reverses the flow of refrigerant through the unit (goes into cooling mode) which heats the coil and melts the frost.  Once the thermo switch reaches 70F on the coil it opens and defrost is shut off.
This is all good but on most systems when defrost is active the registers inside the house will blow cold air (since unit is in cooling mode) unless auxiliary heat is activated, which in most cases activates 5kw - 10kw of heat strips in your air handler so the air isn't cold.  The activated heat strips use way more energy than the heatpump does.

The problem is that in most cases there is no need to defrost and it goes into defrost mode when there is no frost whatsoever on the coil.  It could be 50F outside and the thermo switch see 32F for 1 sec and the unit defrosts at the end of the timer.  The thermo switch once closed will not reopen until it sees 70F.


Smart Heatpumps??:

Some heatpump out there are smarter and use 2 temperature sensors to determine when to defrost.  It monitors the outside/ambient temperature and also the coil temperature of the heatpump.  Normally it won't let defrost activate at all if the outside temp is over 45F or so.  If outside temp is below that threshold then it looks at the coil temp.  Coil temp has to be below a certain threshold as well, we will say 32F for example.  It then uses an algorithm to determine if the difference between the outside temp and the coil temp is bigger then a percentage of the coil temp.  If the difference is big enough and stays that way for a length of time the unit will initiate defrost.  Way smarter than the dumb way and will go into defrost mode way less often (costs less since less aux heat activation).


My Solution:

Using a NodeMCU, 2xDS18B20 temp sensors, a 5V relay board, a 5V 2A phone charger, a piece of pcb, and a small enclosue I have made my heatpump smarter.

Solder the 2 DS18B20 sensors to the pcb with a 4.7K resistor between VCC and data lines.  Connect Vin to 5Vin on the NodeMCU, Data to D2 on NodeMCU, and Grd to grd on Nodemcu.  Connect relay Vin to 5Vin on NodeMCU, data to D1 on nodemcu, and Grd to grd.  Make sure you get your addresses of both of your DS18B20 sensors before proceeding.  I use the tutorial located at https://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html.  Finally the thermo switch wiring will go through the relay so the code can open or close the circuit when needed.  The pin the drives the relay is high when NodeMCU is powered on but defrost is not triggered.  This makes sure that if the NodeMCU or the relay loses power or dies then the heatpump will continue with factory defrosting.

Change the addresses of the sensors in the sketch at lines 77 and 78 to your addresses.  Fill in your Thingspeak information and flash the sketch.  Once Nodemcu is up connect to it's AP (AutoConnectAP) and goto 192.168.4.1 in your browser to connect the device to your home network.  You can test your defrost threshold setting using heated and cooled glasses of water (only with DS18B20 sensors).  Not all threshold settings will work for every area.  For example you might have to change outdoor temp to trigger at 36F or whatever.

I am also using a javascript page (found here: http://community.thingspeak.com/forum/announcements/thingspeak-live-chart-multi-channel-second-axis-historical-data-csv-export/ ) for pulling and monitoring thingspeak data in a much better graph .  You can actually see when the heatpump defrosts by watching the coil temp (it will rise quickly and spike).


My advice:

I suggest getting the project ready and attach sensors to heatpump but do not attach the thermo switch to the relay yet.  This will let you monitor the heatpump for a few days and you can see when your code triggers cheating and possible defrosting while watching the temps.  After you are satisfied you can connect one wire of the thermo switch through the relay.
