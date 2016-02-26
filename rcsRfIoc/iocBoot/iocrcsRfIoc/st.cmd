## Example vxWorks startup file

## The following is needed if your board support package doesn't at boot time
## automatically cd to the directory containing its startup script
cd "/home/RCS/EpicsBak/rcsRfIoc"

ld <bin/vxWorks-mpc8572/rcsRfIoc.munch

epicsEnvSet("EPICS_CA_MAX_ARRAY_BYTES","1640800")

dbLoadDatabase "dbd/rcsRfIoc.dbd"
rcsRfIoc_registerRecordDeviceDriver pdbbase

## Load record instances
dbLoadRecords("db/rcsRf.db", "IOC=rcs:D212, Card=0, Scan=.1 second")

#D212Config(cardNum,index)
D212Config(0,0)

iocInit
