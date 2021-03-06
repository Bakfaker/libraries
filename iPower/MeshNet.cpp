/*
    This file is part of the MeshNet Arduino library.
    Copyright (C) 2013  Mattia Baldani

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


/*
 *
 * This file contains the implementation of Layer3 and Layer4 of MeshNet.
 *
 * The various Layer2 must be implemented in other files.
 *
 */


#include "MeshNet.h"

// Serve solo per il debugging sul computer
//#include <stdio.h>
// Questo invece serve per il debugging sull'arduino tramite la Serial

// For the random function
//#include <stdlib.h>



/*

LAYER 3 PACKET SPECIFICATIONS:
-----------------------------------

   byte 0       byte 1     byte 2     byte 3
  +----------+----------+----------+----------+-----
  | ----TTTT | -------- | -------- | -------- | --
  +----------+----------+----------+----------+-----
where:
    T = type of message:
    	0x0 = data message to base
    	0x1 = data message to device
    	0x2 = beacon (to device)
    	0x3 = beaconChildResponse (to the parent)
    	0x4 = beaconParentResponse (to the base)
    	0x5 = assignAddress (to device)
    	others: reserved for future use, now must be considered invalid and packet discarded
    	
"data message" type:
   byte 0       byte 1     byte 2     byte 3
  +----------+----------+----------+----------+-----
  | -------- | AAAAAAAA | DDDDDDDD | DD.....
  +----------+----------+----------+----------+-----
where:
    A = 
        if "data message to base":   A = source address
        if "data message to device": A = destination address
    D = layer 4 data
    

"beacon" type:
   byte 0       byte 1     byte 2     byte 3     byte 4     byte 5     byte 6
  +----------+----------+----------+----------+----------+----------+----------+
  | ----0010 | NNNNNNNN | NNNNNNNN | BBBBBBBB | BBBBBBBB | BBBBBBBB | BBBBBBBB |
  +----------+----------+----------+----------+----------+----------+----------+
where:
    N = "NetworkID" a 16 bit identificator of this network, shared by all bases and devices of this network
    B = "BaseNonce", a 32 bit random nonce generated by the base, devices must append this in their HMAC key when they send a message to the base
   TODO gli metto anche l'HMAC fatto con "BaseNonce precedente a questo"+"NetworkKey"? In questo modo quando un device si è "agganciato" alla sequenza di beacon, resiste meglio agli attacchi



"beaconChildResponse" type:
   byte 0       byte 1     byte 2     byte 3     byte 4     byte 5     byte 6     byte 7     byte 8
  +----------+----------+----------+----------+----------+----------+----------+----------+----------+
  | ----0011 | CCCCCCCC | CCCCCCCC | CCCCCCCC | CCCCCCCC | HHHHHHHH | HHHHHHHH | HHHHHHHH | HHHHHHHH |
  +----------+----------+----------+----------+----------+----------+----------+----------+----------+
where:
    C = "ChildNonce",  a 32 bit nonce of the child in a "connection" relationship. Base must use them in HMAC to send a message to this node
    H = "HMAC", the first 32 bit of HMAC-SHA1 of this packet, using as a key the combination of "BaseNonce"+"NetworkKey"



"beaconParentResponse" type:
   byte 0       byte 1     byte 2     byte 3     byte 4     byte 5     byte 6     byte 7     byte 8     byte 9                byte 12
  +----------+----------+----------+----------+----------+----------+----------+----------+----------+----------+--... ...-+----------+
  | ----0100 | CCCCCCCC | CCCCCCCC | CCCCCCCC | CCCCCCCC | PPPPPPPP | PPPPPPPP | PPPPPPPP | PPPPPPPP | HHHHHHHH | HH    HH | HHHHHHHH |
  +----------+----------+----------+----------+----------+----------+----------+----------+----------+----------+--... ...-+----------+
where:
    C = "ChildNonce",  a 32 bit nonce of the child in a "connection" relationship. Base must use them in HMAC to send a message to this node
    P = "ParentNonce", a 32 bit nonce of the parent in a "connection" relationship. Base must use them in HMAC to send a message to this node.
    H = "HMAC", the first 32 bit of HMAC-SHA1 of this packet, using as a key the combination of "BaseNonce"+"NetworkKey"



"assignAddress" type:
   byte 0       byte 1     byte 2     byte 3     byte 4     byte 5     byte 6     byte 7     byte 8     byte 9     byte 10
  +----------+----------+----------+----------+----------+----------+----------+----------+----------+----------+----------+
  | ----0101 | CCCCCCCC | CCCCCCCC | CCCCCCCC | CCCCCCCC | AAAAAAAA | MMMMMMMM | HHHHHHHH | HHHHHHHH | HHHHHHHH | HHHHHHHH |
  +----------+----------+----------+----------+----------+----------+----------+----------+----------+----------+----------+
where:
    C = "ChildNonce", the 32 bit nonce of the node of which I want to set the address.
    A = "Address", the address to set to the node which correspond to "ChildNonce"
    M = "MaxRoute", the parent of the node which I'm setting the address, must route to him all messages which have the destination address in the range "Address"-"MaxRoute"
    H = "HMAC", the first 32 bit of HMAC-SHA1 of this packet, using as a key the combination of "BaseNonce"+"ChildNonce"+"NetworkKey"
    
    
Example network setup procedure:
1) Base send a "beacon" packet with "1758" as "BaseNonce". When a device receives them, it save the layer2 address of their next device to the base, and forward the beacon packet to all its network interfaces, specifying "broadcast" as macAddress (we use "0")
2) Every device generate and send their own "beaconChildResponse" packet to their next device to the base, with HMAC generated with "1758" as "BaseNonce".
(If an attacker has sent a beacon with a different BaseNonce, this beaconResponse packet will be discarded by the base, because it has a wrong HMAC)
3) If a device receives a "beaconChildResponse" packet, it first check if the HMAC is valid, if not it will discard the packet. If the HMAC is correct, it will add the layer2 address and "ChildNonce" of the sender of "beaconResponse" to his temporary "Child Table".
Then it will generate a new "beaconParentResponse" packet with the same "ChildNonce" of the previous packet, with his nonce as "ParentNonce", and generates the HMAC of the new packet. Then it will send this new packet to the device from whom has received the last beacon, and that device should route them to the base.
4) When the base receives a "beaconChildResponse" or "beaconParentResponse" packet, it will check the HMAC, then read the data and use them to make a tree of the devices of the network. In the tree the nodes are identified with their Nonce. Then it will generates the address and maxRoute of each node using the CSkip algorithm from Zigbee.
5) The base send an "assignAddress" packet to each node. It first send them to the 1st level nodes, the nodes directly connected to the base, then to 2nd level nodes and so on.
When a device receives an "assignAddress" packet, it will first validate the HMAC using the "ChildNonce" written in the packet and the "BaseNonce" that has previously received in the last beacon. 
Then it will check if the "ChildNonce" is his nonce. If this is true, it will set the "Address" in his myAddress variable.
Else it will chech if in his Child Table there is an entry with the "ChildNonce" written in this packet. If it's present it will remove the corresponding row from the Child Table, then add a row in the Routing Table with as Address the "Address" specified in this packet, as maxRoute the "maxRoute" specified in this packet, as interface and macAddress the ones present in the Child Table row. Then it will relay to this child this "assignAddress" packet without any modification.
Else if there isn't an entry in the Child Table with the "ChildNonce" written in this packet, it will use the "tree routing" algorithm to route this packet using the "address" written in this packet.
    


TODO specifiche vecchie da aggiornare
facciamo che ogni nodo ha un id casuale "quasi univoco", nel senso che potrebbero esserci collisioni, ma solo abbastanza improbabili. Questo id deve essere lungo 4 byte, cioè 32 bit, e bisognerebbe gestire il caso in cui ci sono collisioni, anche se dovrebbero essere dovute solo a dei bug o casini dell'utente, perchè questi id mi sa che conviene che li assegna la base ai device quando fanno il pairing.

Una rete è formata da una o più basi, e deve avere un Network ID da 32 bit che tutte le basi inviano nel beacon, così i device capiscono se quella è la loro rete o è quella di qualcun'altro
*/


// Only for tests, this should be readen from the EEPROM
uint16_t networkId = 10101;
uint32_t networkKey = 80808;


/* Child table - a table used in the network setup phase, when it's completed it can be deleted.
    Columns:
     - ChildNonce
     - interface
     - macAddress
*/
typedef struct {
	uint32_t childNonce;
	uint8_t interface;
	uint8_t macAddress;
} __attribute__((packed)) childTableRow;

#define MAX_CHILD_TABLE_LEN 5
childTableRow childTable[MAX_CHILD_TABLE_LEN]; // TODO andrebbe allocata dinamicamente nello heap...
uint8_t childTableLen = 0; // the current number of rows of the table



// Routing table - contains routes to child devices (not to base)
/*
TODO colonne della tabella:
- address: indirizzo layer 3 del nodo figlio
- maxRoute: estremo del range address-maxRoute di indirizzi che devono essere instradati a questo figlio
- interface: numero della mia interfaccia di rete che devo usare per contattare questo figlio
- macAddress: indirizzo MAC che devo passare a questo specifico layer 2 per contattare questo figlio
*/
typedef struct {
	uint8_t address;
	uint8_t maxRoute;
	uint8_t interface;
	uint8_t macAddress;
} __attribute__((packed)) routingTableRow;

routingTableRow routingTable[5]; // TODO va allocata dinamicamente nello heap
uint8_t routingTableLen = 0; // the current number of rows of the table


// Route to base
uint8_t toBaseInterface = -1; // if == -1 the route to base is not defined
uint8_t toBaseMacAddress;

// My address
uint8_t myAddress;

// Nonces
uint32_t baseNonce;
uint32_t myChildNonce;
typedef struct {
    uint32_t baseNonce;
    uint32_t networkKey;
} __attribute__((packed)) toBaseKey;
typedef struct {
    uint32_t childNonce;
    uint32_t baseNonce;
    uint32_t networkKey;
} __attribute__((packed)) toDeviceKey;

// Temporary new network config data
uint32_t newBaseNonce;
uint8_t newToBaseInterface = -1;
uint8_t newToBaseMacAddress;
uint32_t newMyChildNonce;


 
// Packets structs

#define DATA_TO_BASE 0x00
#define DATA_TO_DEVICE 0x01

#define BEACON_TYPE 0x02
typedef struct {
    const unsigned char type;
    uint16_t networkId;
    uint32_t baseNonce;
} __attribute__((packed)) beacon;


#define BEACON_CHILD_RESPONSE_TYPE 0x03
typedef struct {
    const unsigned char type;
    uint32_t childNonce;
    uint32_t hmac;
} __attribute__((packed)) beaconChildResponse;

#define BEACON_PARENT_RESPONSE_TYPE 0x04
typedef struct {
    const unsigned char type;
    uint32_t childNonce;
    uint32_t parentNonce;
    uint32_t hmac;
} __attribute__((packed)) beaconParentResponse;

#define ASSIGN_ADDRESS_TYPE 0x05
typedef struct {
    const unsigned char type;
    uint32_t childNonce;
    uint8_t address;
    uint8_t maxRoute;
    uint32_t hmac;
} __attribute__((packed)) assignAddress;



/*
TODO per interfacciarmi con i vari layer 2 faccio così:
- ogni "interfaccia di rete" che ho attaccata è identificata da un numero intero da 8 bit
- quando un layer 2 vuole passarmi un pacchetto, deve darmi come indirizzo sorgente un indirizzo MAC che è un intero da da 8 bit
- un mio vicino è quindi identificato dalla coppia "interfaccia di rete"-"indirizzo MAC da 8 bit"
- quando devo inviare un pacchetto ad un mio vicino, dal numero di interfaccia di rete capisco che funzione handler di uno specifico layer 2 devo chiamare, quindi chiamo quella funzione e gli passo come parametro l'indirizzo MAC.
- i layer 2 possono tenersi una tabella che fa corrispondere ogni "indirizzo MAC" usato dal layer 3 un indirizzo hardware usato proprio da quel specifico protocollo di rete, oppure possono fare delle funzioni che convertono in modo deterministico l'"indirizzo MAC" con l'indirizzo hardware vero e proprio
- tutti i layer2 devono considerare il macAddress=0 come indirizzo di broadcast. Quindi quando invio un pacchetto al macAddress 0, il layer2 deve inviarlo a tutti i device che lui è in grado di contattare, se quel layer2 non supporta i broadcast ma solo gli unicast, vorrà dire che il layer2 dovrà occuparsi di inviare un pacchetto in unicast ad ognuno dei device che è in grado di contattare.
- quando ricevo un pacchetto che il device mittente l'ha inviato in broadcast, io devo essere in grado di sapere qual'è il macAddress vero del device mittente, così potrò rispondere solo a lui. Quindi praticamente il macAddress 0 di broadcast può essere usato solo in invio, non in ricezione.
*/


int printPacket(unsigned char *packet, uint8_t len){
    int i;
    for(i=0; i<len; i++){
    	//printf("%u ", packet[i]);
    	DEBUG_PRINT(packet[i]);
    	DEBUG_PRINT(" ");
    }
    //printf("\n");
    DEBUG_PRINTLN(" ");
}


uint32_t calculateHmac(unsigned char *packet, size_t len, unsigned char *key, size_t keyLen){
    /*unsigned char hmac[20];
    hmac_sha1(&hmac, key, (uint16_t)keyLen, packet, (uint32_t)len);
    return *((uint32_t *) &hmac);*/
    return 100;
}




/*
-------- LAYER 4 SPECIFICATIONS ---------

This is a layer4 packet:

   byte 0       byte 1     byte 2     byte 3    
  +----------+----------+----------+----------+----
  | CCCCCCCC | DDDDDDDD | DDDDDDDD | DDDDDDDD | ..
  +----------+----------+----------+----------+----
where:
    C = command
    P = data
    
The layer4 is a very simple RPC protocol. When a packet is received, it will call the procedure (a sort of layer7) that correspond to the "command" written in the packet, passing as an argument a pointer to the first "data" byte, and the maximum length of the packet (the packet might be smaller than this maximum lenght!!).

The layer7 (the procedure called) is free to do whatever he wants, however there are some recommended guidelines:
- The procedure called will put the data in a struct, read the values it needs, perform an action, and reply with a packet with the same "command", and with some data that might be an ACK or a response to the request.
- For every "command" there should be a struct of the "base-to-device" packet and the "device-to-base" packet. Usually one is the request, and one is the response
- Messages should be "idempotent": if I send a message 5 times (for example to be sure that the receiver reads them), the receiver should not repeat some action 5 times. To do that it might be necessary to include a "transaction id" or "sequence number".

Every device must have this procedure at "command 0":
When the device receives a command 0 packet, it immediatly send a packet to the base with command 0 and this content:
1 byte: the copy of the first byte of the received message
4 byte: and unsigned 32-bit number which is the "deviceType".

The "deviceType" is an unique number that identifies the capability of this device: for example the kind of "command" he can receive, and indirectly his hardware resources (sensors, actuators, ...).

*/

typedef struct{
    unsigned char type;
    uint8_t srcAddress;
    uint8_t command;
    unsigned char data[40]; // TODO lunghezza array messa a caso
} __attribute__((packed)) dataToBaseLayer4;

// Send a layer4 packet to base
void sendCommand(uint8_t command, void* data, uint8_t dataLen){
    if(toBaseInterface != -1){
        dataToBaseLayer4 message;
	message.type = DATA_TO_BASE;
        message.srcAddress = myAddress;
        message.command = command;
        memcpy(&message.data, data, dataLen);
        sendPacket((unsigned char*)&message, dataLen+3, toBaseInterface, toBaseMacAddress);
    }
}


// Standard command 0 (layer7)

typedef struct {
    uint32_t deviceType;
    uint32_t deviceUniqueId;
} deviceInfoCommand __attribute__((packed));

// Send the command 0 to the base (should be done when I receive an assignAddress packet or when base send me a layer4 command 0)
void sendDeviceInfoCommand(){
    deviceInfoCommand data;
    data.deviceType = deviceType;
    data.deviceUniqueId = deviceUniqueId;
    sendCommand(0,(unsigned char *) &data, sizeof(data));
}


// Handles an incoming layer4 packet
void handleDataPacket(unsigned char* message, uint8_t len){
    if(*message == 0x00 && len >= sizeof(deviceInfoCommand)){
        sendDeviceInfoCommand();
    } else {
        unsigned char* data = message+1;
        onCommandReceived((uint8_t)*message, (void*) data, len-1);
    }
}





// Find the layer2 address and interface to reach a child, using the "tree routing"
int treeRouteToChild(uint8_t address, uint8_t *interface, uint8_t *macAddress){
    int i;
    for(i=0; i<routingTableLen; i++){
    	if(address >= routingTable[i].address && address <= routingTable[i].maxRoute){
    	    *(interface) = routingTable[i].interface;
    	    *(macAddress) = routingTable[i].macAddress;
    	    return 1;
    	}
    }
    return 0;
}


// Pass a layer2 packet to the layer3
// Max message size: 256 bytes, because len is 8 bit
void processIncomingPacket(unsigned char* message, uint8_t len, uint8_t interface, uint8_t macAddress){

    DEBUG_PRINT("processIncomingPacket len: ");
    DEBUG_PRINT(len);
    DEBUG_PRINT(" interface: ");
    DEBUG_PRINT(interface);
    DEBUG_PRINT(" macAddress: ");
    DEBUG_PRINTLN(macAddress);
    DEBUG_PRINT("packet: ");
    printPacket(message, len);

    // Check if bigger than minimum size
    if(len<3){
    	return;
    }
    // Check if macAddress is invalid (we can use the broadcast macAddress 0 only when transmitting, not receiving)
    if(macAddress==0){
        return;
    }

    // Check the Layer 3 packet type
    unsigned char msgType = message[0];
    msgType = msgType & 0x0F;
    if(msgType == 0x00){
    	// Normal message to base
    	
    	if(toBaseInterface != -1){
    	    sendPacket(message, len, toBaseInterface, toBaseMacAddress);
    	} else {
    	    // No path to base defined, dropping packet
    	}
    	
    } else if(msgType == 0x01){
        // Normal message to device
        
    	if(message[1] == myAddress){
    	    // I'm the destination!! Pass the packet to the upper layer4
    	    handleDataPacket(message+2, len-2);
    	} else {
    	    uint8_t childInt;
    	    uint8_t childMac;
    	    if(treeRouteToChild(message[1], &childInt, &childMac)){
    	    	// Send the packet to the child
    	    	sendPacket(message, len, childInt, childMac);
    	    } else {
    	    	// I don't have a route for this device, dropping packet
    	    	printf("Destination unknown, dropping data packet\n");
    	    }
    	}
    	
    } else if(msgType == 0x02){
    
        // Beacon
        
        DEBUG_PRINT("beaconRicevuto!");
        // TODO check message lenght
        beacon *rec = (beacon *) message;
        if(rec->networkId == networkId){
            
            // Save new temporary network config
            if(rec->baseNonce == newBaseNonce){
                return;
            }
            newBaseNonce = rec->baseNonce;
            newToBaseInterface = interface;
            newToBaseMacAddress = macAddress;
            newMyChildNonce = random();
            // Broadcast the beacon to all interfaces
            int interf;
            for(interf=0; interf<NUM_INTERFACES; interf++){
                sendPacket((unsigned char *) rec, sizeof(beacon), interf, 0); // 0 is broadcast macAddress
            }
            // TODO maybe we could insert a small delay here to ensure the propagation of beacons without colliding with beaconResponses
            // Send a new beaconChildResponse
            beaconChildResponse resp = {BEACON_CHILD_RESPONSE_TYPE};
            resp.childNonce = newMyChildNonce;
            toBaseKey key;
            key.baseNonce = newBaseNonce;
            key.networkKey = networkKey;
            resp.hmac = calculateHmac((unsigned char *) &resp, sizeof(beaconChildResponse)-4, (unsigned char *) &key, sizeof(key));
            sendPacket((unsigned char *) &resp, sizeof(resp), newToBaseInterface, newToBaseMacAddress);
        }
        
    } else if(msgType == 0x03){
    
        // Beacon child response

        if(sizeof(beaconChildResponse)!=len){
            return;
        }
        beaconChildResponse *rec = (beaconChildResponse *) message;
        toBaseKey key;
        key.baseNonce = newBaseNonce;
        key.networkKey = networkKey;
        uint32_t genHmac = calculateHmac((unsigned char *) rec, sizeof(beaconChildResponse)-4, (unsigned char *) &key, sizeof(key));
        if(genHmac != rec->hmac){
            printf("Invalid hmac!\n");
            return;
        }
        // Valid packet, add a row in childTable
        if(childTableLen == MAX_CHILD_TABLE_LEN){
            return;
        }
        childTable[childTableLen].childNonce = rec->childNonce;
        childTable[childTableLen].interface = interface;
        childTable[childTableLen].macAddress = macAddress;
        childTableLen++;
        // Send beaconParentResponse packet
        beaconParentResponse resp = {BEACON_PARENT_RESPONSE_TYPE};
        resp.childNonce = rec->childNonce;
        resp.parentNonce = newMyChildNonce;
        resp.hmac = calculateHmac((unsigned char *) &resp, sizeof(resp)-4, (unsigned char *) &key, sizeof(key));
        sendPacket((unsigned char *) &resp, sizeof(resp), newToBaseInterface, newToBaseMacAddress);
        
    } else if(msgType == 0x04){
    
        // Beacon parent response
        
        // here we could optionally check if HMAC is valid
        // Route the packet to the base
        if(newToBaseInterface != -1){
            sendPacket(message, len, newToBaseInterface, newToBaseMacAddress);
        }
        
    } else if(msgType == 0x05){
    
        // assignAddress
        
        assignAddress *rec = (assignAddress *) message;
        if(len != sizeof(assignAddress)){
            return;
        }
        // Check the HMAC
        toDeviceKey key;
        key.childNonce = rec->childNonce;
        key.baseNonce = newBaseNonce;
        key.networkKey = networkKey;
        uint32_t genHmac = calculateHmac((unsigned char *) rec, sizeof(rec)-4, (unsigned char *) &key, sizeof(key));
        if(genHmac != rec->hmac){
            DEBUG_PRINT("wronghmac, rechmac:");
            printPacket((unsigned char *)&rec->hmac, 4);
            DEBUG_PRINT(",calchmac:");
            printPacket((unsigned char *)&genHmac, 4);
            return;
        }
        // Check if ChildNonce in packet is my nonce
        if(rec->childNonce == newMyChildNonce){
            DEBUG_PRINT("ismychildnonce!");
            myAddress = rec->address;
            // I switch myself to the new network configuration!
            baseNonce = newBaseNonce;
            toBaseInterface = newToBaseInterface;
            toBaseMacAddress = newToBaseMacAddress;
            myChildNonce = newMyChildNonce;
            // Send the layer7 standard command 0 to the base
            sendDeviceInfoCommand();
            return;
        }
        // Check if ChildNonce is a child of mine
        int i;
        for(i=0; i<childTableLen; i++){
            if(rec->childNonce == childTable[i].childNonce){
                // Add this child to the routingTable
                routingTable[routingTableLen].address = rec->address;
                routingTable[routingTableLen].maxRoute = rec->maxRoute;
                routingTable[routingTableLen].interface = childTable[i].interface;
                routingTable[routingTableLen].macAddress = childTable[i].macAddress;
                routingTableLen++;
                // Remove this child from the childTable
                childTableLen--;
                // Forward the packet to the child
                sendPacket((unsigned char *) rec, len, routingTable[routingTableLen-1].interface, routingTable[routingTableLen-1].macAddress);
                return;
            }
        }
        // The ChildNonce maybe is of a child of a child, I try to use the "tree routing" to route this packet
        uint8_t interfaceRes;
        uint8_t macAddressRes;
        if(treeRouteToChild(rec->address, &interfaceRes, &macAddressRes)){
            sendPacket(message, len, interfaceRes, macAddressRes);
        } else {
            // Unable to find the route to this packet!! discarding packet
        }
        
    } else {
    	// inexistent type, this packet must be dropped!
    	return;
    }
    
    return;
}


int sendDebugPacket(unsigned char *packet, uint8_t len, uint8_t interface, uint8_t macAddress){
    printPacket(packet, len);
    processIncomingPacket(packet, len, interface, macAddress);
}

void printDebugStateInfo(){
	//printf("myAddress: %d toBaseInterface: %d toBaseMacAddress: %d\n", myAddress, toBaseInterface, toBaseMacAddress);
	DEBUG_PRINT("myAddress: ");
	DEBUG_PRINT(myAddress);
	DEBUG_PRINT(" toBaseInterface: ");
	DEBUG_PRINT(toBaseInterface);
	DEBUG_PRINT(" toBaseMacAddress: ");
	DEBUG_PRINTLN(toBaseMacAddress);
	//printf("routingTableLen: %d\n", routingTableLen);
	DEBUG_PRINT("routingTableLen: ");
	DEBUG_PRINTLN(routingTableLen);
	int i;
	for(i=0; i<routingTableLen; i++){
	    printPacket((unsigned char *)&routingTable[i], sizeof(routingTableRow));
	}
	printf("childTableLen: %d\n", childTableLen);
	for(i=0; i<childTableLen; i++){
	    printPacket((unsigned char *)&childTable[i], sizeof(childTableRow));
	}
	DEBUG_PRINT("newToBaseInterface: ");
	DEBUG_PRINT(newToBaseInterface);
	DEBUG_PRINT(" newToBaseMacAddress: ");
	DEBUG_PRINTLN(newToBaseMacAddress);
}


#ifdef PC_TEST 

int main(){
	printf("Inizio programma\n");
	
	printDebugStateInfo();
	
	unsigned char mess[7] = {BEACON_TYPE, 0x6F, 0x47, 0x11, 0x11, 0x11, 0x22};
	sendDebugPacket(mess, 7, 0, 1);
	
	unsigned char mess3[9] = {BEACON_CHILD_RESPONSE_TYPE, 0, 0, 0, 2, 100, 0, 0, 0};
	sendDebugPacket(mess3, 9, 0, 2);
	
	unsigned char mess2[11] = {ASSIGN_ADDRESS_TYPE, 103, 69, 139, 107, 1, 3, 100, 0, 0, 0};
	sendDebugPacket(mess2, 11, 0, 1);
	
	printDebugStateInfo();
	
	printf("Fine programma\n");
}

#endif
