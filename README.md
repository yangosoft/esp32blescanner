# Bluetooth Scanner

ESP32 Bluetooth scanner that sends scanning results to a server


- [ESP32](esp32ble/)
- [Backend](blestorer/)
- [Processing scripts](processing_scripts)



```
$ for f in output/*.txt ; do python3 insert.py ${f} --db analisys.sqlite3 ; done
$ python3 plot_macs.py --db analisys.sqlite3
```
