#include <Wire.h> //protocolo I2C
#include "SparkFunMPL3115A2.h" //Librería para sensor de presión (Placa SparkFun)
#include "SparkFunHTU21D.h" //Librería para sensor de humedad (Placa SparkFun)

MPL3115A2 sens_presion;  //instancia para sensor de presión
HTU21D sens_humedad; //Instancia para sensor de humedad

//-------- Definiciones de pines hardware ----------//

// digitales
const byte VELVIENTO = 3;
const byte LLUVIA = 2;
const byte STAT1 = 7;
const byte STAT2 = 8;

// analogicos
const byte DIRVIENTO = A0;
const byte LUZ = A1;
const byte BATERIA = A2;
const byte REFERENCIA_3V3 = A3;
//--------------------------------------------------//

//---------- Variables globales --------------------//

//Variables básicas
int dirViento = 0;
float velViento = 0;
float humedad = 0;
float temp = 0;
float lloviendo = 0;
float presion = 0;

//Variables para calcular tiempo
long ultSegundo;
byte segundos;
byte segundos2m;//Para calcular la media de dirección y velocidad del viento durante los 2 últimos minutos
int segundos5m;//Para calcular los segundos para el envío de datos
byte minutos;
byte minutos10m;//Para calcular la media de dirección y velocidad del viento durante los 10 últimos minutos

//Variables para chequeos del viento
long ultimoChequeoViento = 0;
volatile long ultimoIRQViento = 0; 
volatile byte clicsViento = 0;

//Resto variables (media viento, rafaga máx, ...)
float rafagaVientoMax = 0;
int dirRafagaVientoMax = 0;
float mediaVelViento2m = 0;
int mediaDirViento2m = 0;
float maxVelViento10m = 0;
int maxDirViento10m = 0;
volatile float lluviaDia = 0;
volatile unsigned long tiempoLluvia, ultimaLluvia, intervaloLluvia, lluvia;

//--------------------------------------------------//

//------------- Arrays para almacenar medias -------//

byte mediaVelVientoArray[120]; //120 bytes para almacenar 2 minutos 
int mediaDirVientoArray[120];
float velRafagaMax10m[10];//1 cada minuto durante 10 minutos
int dirRafagaMax10m[10];
volatile float lluviaHora[60];//Lluvia durante la ultima hora


//------------- Interrupt Requests (Anemómetro y pluviómetro --------------//
void lluviaIRQ(){
  
  tiempoLluvia = millis();
  intervaloLluvia = tiempoLluvia - ultimaLluvia; // calculamos intervalo entre éste y el último lluviaIRQ
  
  if(intervaloLluvia > 10){
    lluviaDia += 0.011; //Cada IRQ equivale a 0.011" de lluvia
    lluviaHora[minutos] += 0.011;
    
    ultimaLluvia = tiempoLluvia;//Preparamos para siguiente IRQ
  }       
}
  
void velVientoIRQ(){
  if (millis() - ultimoIRQViento > 10){
    ultimoIRQViento = millis();
    clicsViento++;  
  }
}


void setup() {
  Serial.begin(9600);
  Serial.println("Meteomenorca v0.5.1");
  
  pinMode(STAT1, OUTPUT);
  pinMode(STAT2, OUTPUT);
  pinMode(VELVIENTO,INPUT_PULLUP);
  pinMode(LLUVIA, INPUT_PULLUP);
  pinMode(REFERENCIA_3V3, INPUT);
  pinMode(LUZ, INPUT);
  
  sens_presion.begin(); //inicializamos sensor presión
  sens_presion.setModeBarometer();
  sens_presion.setOversampleRate(7);
  sens_presion.enableEventFlags();
  
  sens_humedad.begin();//inicializamos sensor humedad
  
  segundos = 0;
  ultSegundo = millis();
  
  attachInterrupt(0, lluviaIRQ, FALLING);//Interruptores para IRQ de anemómetro y pluviómetro
  attachInterrupt(1, velVientoIRQ, FALLING);
  interrupts();//Activamos interruptores
  
  Serial.println("Meteomenorca ONLINE!");
  
}

void loop() {
   
  if (millis() - ultSegundo >= 1000){
     
     digitalWrite(STAT1, HIGH);
     
     ultSegundo += 1000;
     
     if(++segundos2m > 119) segundos2m = 0;
     //Calculamos la velocidad y dirección del viento cada dos segundos durante 2 minutos para la media
     float velActual = velViento;
     int dirActual = getDireccionViento();
     //Almacenamos la velocidad actual y dirección actual en cada posicion correspondiente de su array para la media
     mediaVelVientoArray[segundos2m] = (int)velActual;
     mediaDirVientoArray[segundos2m] = dirActual;
     
     if (velActual > velRafagaMax10m[minutos10m]){
         velRafagaMax10m[minutos10m] = velActual;
         dirRafagaMax10m[minutos10m] = dirActual;
      }
      
      if (velActual > rafagaVientoMax){
        rafagaVientoMax = velActual;
        dirRafagaVientoMax = dirActual;
      }
      
      //Reseteo de los contadores de segundos y minutos
      if (++segundos > 59){
          segundos = 0;
          if (++minutos > 59) minutos = 0;
          if (++minutos10m > 9) minutos10m = 0;
          
          lluviaHora[minutos] = 0;
          velRafagaMax10m[minutos10m] = 0;
      }
      
      //Enviamos datos cada 5 minutos
      if (++segundos5m > 299){
        
        enviarDatos();
        
        segundos5m = 0;
      
      }
      
      imprimirTiempo();
      
      digitalWrite(STAT1, LOW);
  }
  delay(100);
}

void calcularTiempo(){
    
    dirViento = getDireccionViento();
    velViento = getVelocidadViento();
    
    //Media de 5 ultimos minutos de viento
    float aux = 0;
    for(int i = 0; i < 120; i++) aux += mediaVelVientoArray[i];
    aux /= 120.0;
    mediaVelViento2m = aux;
    
    //Formula para calcular la media de la dirección del viento
    //sacada de http://stackoverflow.com/questions/1813483/averaging-angles-again
    long sum = mediaDirVientoArray[0];
    int D = mediaDirVientoArray[0];
    for(int i = 1 ; i < 120 ; i++){
	int delta = mediaDirVientoArray[i] - D;

	if(delta < -180)
		D += delta + 360;
	else if(delta > 180)
		D += delta - 360;
	else
		D += delta;

		sum += D;
    }
	mediaDirViento2m = sum / 120;
	if(mediaDirViento2m >= 360) mediaDirViento2m -= 360;
	if(mediaDirViento2m < 0) mediaDirViento2m += 360;
    
    //Buscamos la ráfaga max y su dirección los ultimos 10 min.
    maxVelViento10m = 0;
    maxDirViento10m = 0;
    for (int i = 0; i < 10; i++){
      if (velRafagaMax10m[i] > maxVelViento10m){
          maxVelViento10m = velRafagaMax10m[i];
          maxDirViento10m = dirRafagaMax10m[i];     
      }
    }
    
    //Sacamos la humedad del sensor de humedad
    humedad = sens_humedad.readHumidity();
    
    //Presión 
    presion = sens_presion.readPressure();
    
    //Sacamos temperatura en ºF
    temp = sens_presion.readTempF();
   
     //Calculamos la lluvia de la ultima hora
     lloviendo = 0;
     for (int i = 0; i < 60; i++) lloviendo += lluviaHora[i];
     
     //Pendiente por hacer nivel de luz y de batería *************************************************************
     //***********************************************************************************************************
     //***********************************************************************************************************
     //***********************************************************************************************************
     
     
     //***********************************************************************************************************
     //***********************************************************************************************************
     
     //***********************************************************************************************************
     //***********************************************************************************************************
     
     //***********************************************************************************************************
     //***********************************************************************************************************
}


float getVelocidadViento(){
    	
  	float deltaTime = millis() - ultimoChequeoViento;
        
	deltaTime /= 1000.0; //Pasamos a segundos

	float viento = (float)clicsViento / deltaTime;

        clicsViento = 0; //Reseteamos clics del anemómetro
        
        viento *= 1.492; //a MPH
        
	ultimoChequeoViento = millis();
        
        return(viento);
	
}

int getDireccionViento(){
  
        unsigned int adc;

	adc = analogRead(DIRVIENTO);

        //Segun voltaje que nos devuelva un valor que será los º aprox a los que señala
	if (adc < 380) return (113);
	if (adc < 393) return (68);
	if (adc < 414) return (90);
	if (adc < 456) return (158);
	if (adc < 508) return (135);
	if (adc < 551) return (203);
	if (adc < 615) return (180);
	if (adc < 680) return (23);
	if (adc < 746) return (45);
	if (adc < 801) return (248);
	if (adc < 833) return (225);
	if (adc < 878) return (338);
	if (adc < 913) return (0);
	if (adc < 940) return (293);
	if (adc < 967) return (315);
	if (adc < 990) return (270);
	return (-1); //Si hay error
}

void imprimirTiempo(){
      	calcularTiempo();

	Serial.println();
	Serial.print("$,dirViento=");
	Serial.print(dirViento);
	Serial.print(",velViento=");
	Serial.print(velViento, 1);
	Serial.print(",rafagaVientoMax=");
	Serial.print(rafagaVientoMax, 1);
	Serial.print(",dirRafagaMax=");
	Serial.print(dirRafagaVientoMax);
	Serial.print(",mediaVelViento5m=");
	Serial.print(mediaVelViento2m, 1);
	Serial.print(",mediaDirViento5m=");
	Serial.print(mediaDirViento2m);
	Serial.print(",maxVelViento10m=");
	Serial.print(maxVelViento10m, 1);
	Serial.print(",maxDirViento10m=");
	Serial.print(maxDirViento10m);
	Serial.print(",humedad=");
	Serial.print(humedad, 1);
	Serial.print(",tempC=");
	Serial.print((temp - 32) * 5/9 , 1);
	Serial.print(",lloviendo=");
	Serial.print(lloviendo * 25.4, 2);
	Serial.print(",lluviaDia=");
	Serial.print(lluviaDia * 25.4, 2);
	Serial.print(",presion=");
	Serial.print(presion / 100, 2);
	Serial.print(",");
	Serial.println(segundos5m); 

}

void enviarDatos(){

  Serial.println("Datos enviados!");

}
