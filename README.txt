--------------------------------------------
-- STM32 Bootloader
--
-- Johannes Hübner <dev@johanneshuebner.com>
--------------------------------------------

This boot loader does not interact with the built-in STM32 boot loader. It 
uses an interface of your choice, right now this is USART3. The 
interface flexibility and the independence from the BOOT pins are the 
main reasons for implementing this boot loader.

New update protocol
- 115200-8-N-2 (2 stop bits necessary when using a ZigBee module)

1 Send '2' indicating version 2 bootloader, wait about 500ms for magic byte 0xAA
2 If no reply goto 7
3 Send an 'S' indicating that it is awaiting an update size in pages
4 If no reply within about 500ms go to step 7
4.1 otherwise send a 'P' indicating that it is awaiting the actual page 
as a binary image
4.2 When page not received within about 1s, print 'T' and keep waiting
5 When page received send a 'C' indicating that it is awaiting the pages 
checksum
6 When checksum is correct and more pages need to be received, go to 
step 4.1
6.1 if all pages have been received go to step 5
6.2 When checksum isn't correct print an 'E' then go to step 4.1
7 When done print a 'D' and start main firmware

Notes:
- By checksum I mean the one calculated by the STMs integrated CRC32 unit.
- The actual firmware has a reset command the cycle through the bootloader
- The main firmware must be linked to start at address 0x08001000
- The bootloader starts at address 0x08000000 and can be 4k in size 
(right now its around 2.5k)

