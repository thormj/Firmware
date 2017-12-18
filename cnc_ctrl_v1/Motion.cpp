/*This file is part of the Maslow Control Software.
    The Maslow Control Software is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Maslow Control Software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with the Maslow Control Software.  If not, see <http://www.gnu.org/licenses/>.
    
    Copyright 2014-2017 Bar Smith*/

// This contains all of the Motion commands

#include "Maslow.h"

// Flag for when to send movement commands
volatile bool  movementUpdated  =  false;
// Global variables for misloop tracking
#if misloopDebug > 0
  volatile bool  inMovementLoop   =  false;
  volatile bool  movementFail     =  false;
#endif

void initMotion(){
    // Called on startup or after a stop command
    leftAxis.stop();
    rightAxis.stop();
    if(sys.zAxisAttached){
      zAxis.stop();
    }
}

float calculateDelay(const float& stepSizeMM, const float& feedrateMMPerMin){
    /*
    Calculate the time delay in microseconds between each step for a given feedrate
    */
    
    return LOOPINTERVAL;
}

float calculateFeedrate(const float& stepSizeMM, const float& usPerStep){
    /*
    Calculate the time delay between each step for a given feedrate
    */
    
    #define MINUTEINUS 60000000.0
    
    // derivation: ms / step = 1 min in ms / dist in one min
    
    float tempFeedrate = (stepSizeMM*MINUTEINUS)/usPerStep;
    
    return tempFeedrate;
}

float computeStepSize(const float& MMPerMin){
    /*
    
    Determines the minimum step size which can be taken for the given feed-rate
    based on the loop interval frequency.  Converts to MM per microsecond first,
    then mutiplies by the number of microseconds in each loop interval
    
    */
    return LOOPINTERVAL*(MMPerMin/(60 * 1000000));
}
 
void movementUpdate(){
  #if misloopDebug > 0
  if (movementFail){
    Serial.println("Movement loop failed to complete before interrupt.");
    movementFail = false;
  }
  #endif
  movementUpdated = true;
}


// why does this return anything
int   coordinatedMove(const float& xEnd, const float& yEnd, const float& zEnd, float MMPerMin){
    
    /*The move() function moves the tool in a straight line to the position (xEnd, yEnd) at 
    the speed moveSpeed. Movements are correlated so that regardless of the distances moved in each 
    direction, the tool moves to the target in a straight line. This function is used by the G00 
    and G01 commands. The units at this point should all be in mm or mm per minute*/
    
    float  xStartingLocation = sys.xPosition;
    float  yStartingLocation = sys.yPosition;
    float  zStartingLocation = zAxis.read();  // I don't know why we treat the zaxis differently
    float  zMAXFEED          = MAXZROTMIN * abs(zAxis.getPitch());
    
    //find the total distances to move
    float  distanceToMoveInMM         = sqrt(  sq(xEnd - xStartingLocation)  +  sq(yEnd - yStartingLocation)  + sq(zEnd - zStartingLocation));
    float  xDistanceToMoveInMM        = xEnd - xStartingLocation;
    float  yDistanceToMoveInMM        = yEnd - yStartingLocation;
    float  zDistanceToMoveInMM        = zEnd - zStartingLocation;
    
    //compute feed details
    MMPerMin = constrain(MMPerMin, 1, MAXFEED);   //constrain the maximum feedrate, 35ipm = 900 mmpm
    float  stepSizeMM           = computeStepSize(MMPerMin);
    float  finalNumberOfSteps   = abs(distanceToMoveInMM/stepSizeMM);
    float  delayTime            = calculateDelay(stepSizeMM, MMPerMin);
    float  zFeedrate            = calculateFeedrate(abs(zDistanceToMoveInMM/finalNumberOfSteps), delayTime);
    
    //throttle back federate if it exceeds zaxis max
    if (zFeedrate > zMAXFEED){
      float  zStepSizeMM        = computeStepSize(zMAXFEED);
      finalNumberOfSteps        = abs(zDistanceToMoveInMM/zStepSizeMM);
      stepSizeMM                = (distanceToMoveInMM/finalNumberOfSteps);
      MMPerMin                  = calculateFeedrate(stepSizeMM, delayTime);
    }
    
    // (fraction of distance in x direction)* size of step toward target
    float  xStepSize            = (xDistanceToMoveInMM/finalNumberOfSteps);
    float  yStepSize            = (yDistanceToMoveInMM/finalNumberOfSteps);
    float  zStepSize            = (zDistanceToMoveInMM/finalNumberOfSteps);
    
    //attach the axes
    leftAxis.attach();
    rightAxis.attach();
    if(sys.zAxisAttached){
      zAxis.attach();
    }
    
    float aChainLength;
    float bChainLength;
    float zPosition                   = zStartingLocation;
    long   numberOfStepsTaken         =  0;
    
    while(numberOfStepsTaken < finalNumberOfSteps){
      
        #if misloopDebug > 0
        inMovementLoop = true;
        #endif
        //if last movment was performed start the next
        if (!movementUpdated) {
            //find the target point for this step
            // This section ~20us
            sys.xPosition +=  xStepSize;
            sys.yPosition +=  yStepSize;
            zPosition += zStepSize;
            
            //find the chain lengths for this step
            // This section ~180us
            kinematics.inverse(sys.xPosition,sys.yPosition,&aChainLength,&bChainLength);
            
            //write to each axis
            // This section ~180us
            leftAxis.write(aChainLength);
            rightAxis.write(bChainLength);
            if(sys.zAxisAttached){
              zAxis.write(zPosition);
            }
            
            movementUpdate();
            
            //increment the number of steps taken
            numberOfStepsTaken++;
            
            // Run realtime commands
            execSystemRealtime();
            if (sys.stop){return 1;}
        }
    }
    #if misloopDebug > 0
    inMovementLoop = false;
    #endif
    
    kinematics.inverse(xEnd,yEnd,&aChainLength,&bChainLength);
    leftAxis.endMove(aChainLength);
    rightAxis.endMove(bChainLength);
    if(sys.zAxisAttached){
      zAxis.endMove(zPosition);
    }
    
    sys.xPosition = xEnd;
    sys.yPosition = yEnd;
    
    return 1;
    
}

void  singleAxisMove(Axis* axis, const float& endPos, const float& MMPerMin){
    /*
    Takes a pointer to an axis object and moves that axis to endPos at speed MMPerMin
    */
    
    float startingPos          = axis->read();
    float moveDist             = endPos - startingPos; //total distance to move
    
    float direction            = moveDist/abs(moveDist); //determine the direction of the move
    
    float stepSizeMM           = computeStepSize(MMPerMin);                    //step size in mm

    //the argument to abs should only be a variable -- splitting calc into 2 lines
    long finalNumberOfSteps    = abs(moveDist/stepSizeMM);      //number of steps taken in move
    finalNumberOfSteps = abs(finalNumberOfSteps);
    stepSizeMM = stepSizeMM*direction;
    
    long numberOfStepsTaken    = 0;
    
    //attach the axis we want to move
    axis->attach();
    
    float whereAxisShouldBeAtThisStep = startingPos;
    #if misloopDebug > 0
    inMovementLoop = true;
    #endif
    while(numberOfStepsTaken < finalNumberOfSteps){
        if (!movementUpdated) {
          //find the target point for this step
          whereAxisShouldBeAtThisStep += stepSizeMM;
          
          //write to axis
          axis->write(whereAxisShouldBeAtThisStep);
          movementUpdate();
          
          // Run realtime commands
          execSystemRealtime();
          if (sys.stop){return;}
          
          //increment the number of steps taken
          numberOfStepsTaken++;
        }
    }
    #if misloopDebug > 0
    inMovementLoop = false;
    #endif
    
    axis->endMove(endPos);
    
}

// why does this return anything
int   arc(const float& X1, const float& Y1, const float& X2, const float& Y2, const float& centerX, const float& centerY, const float& MMPerMin, const float& direction){
    /*
    
    Move the machine through an arc from point (X1, Y1) to point (X2, Y2) along the 
    arc defined by center (centerX, centerY) at speed MMPerMin
    
    */
    
    //compute geometry 
    float pi                     =  3.1415;
    float radius                 =  sqrt( sq(centerX - X1) + sq(centerY - Y1) ); 
    float circumference          =  2.0*pi*radius;
    
    float startingAngle          =  atan2(Y1 - centerY, X1 - centerX);
    float endingAngle            =  atan2(Y2 - centerY, X2 - centerX);
    
    //compute angle between lines
    float theta                  =  endingAngle - startingAngle;
    if (direction == COUNTERCLOCKWISE){
        if (theta <= 0){
            theta += 2*pi;
        }
    }
    else {
        //CLOCKWISE
        if (theta >= 0){
            theta -= 2*pi;
        }
    }
    
    float arcLengthMM            =  circumference * (theta / (2*pi) );
    
    //set up variables for movement
    long numberOfStepsTaken       =  0;
    
    float stepSizeMM             =  computeStepSize(MMPerMin);

    //the argument to abs should only be a variable -- splitting calc into 2 lines
    long   finalNumberOfSteps     =  arcLengthMM/stepSizeMM;
    //finalNumberOfSteps = abs(finalNumberOfSteps);
    
    //Compute the starting position
    float angleNow = startingAngle;
    float degreeComplete = 0.0;
    
    float aChainLength;
    float bChainLength;
    
    //attach the axes
    leftAxis.attach();
    rightAxis.attach();
    
    while(numberOfStepsTaken < abs(finalNumberOfSteps)){
        #if misloopDebug > 0
        inMovementLoop = true;
        #endif
        
        //if last movement was performed start the next one
        if (!movementUpdated){
            
            degreeComplete = float(numberOfStepsTaken)/float(finalNumberOfSteps);
            
            angleNow = startingAngle + theta*direction*degreeComplete;
            
            sys.xPosition = radius * cos(angleNow) + centerX;
            sys.yPosition = radius * sin(angleNow) + centerY;
            
            kinematics.inverse(sys.xPosition,sys.yPosition,&aChainLength,&bChainLength);
            
            leftAxis.write(aChainLength);
            rightAxis.write(bChainLength); 
            movementUpdate();
            
            // Run realtime commands
            execSystemRealtime();
            if (sys.stop){return 1;}

            numberOfStepsTaken++;
        }
    }
    #if misloopDebug > 0
    inMovementLoop = false;
    #endif
    
    kinematics.inverse(X2,Y2,&aChainLength,&bChainLength);
    leftAxis.endMove(aChainLength);
    rightAxis.endMove(bChainLength);
    
    sys.xPosition = X2;
    sys.yPosition = Y2;
    
    return 1;
}

void  holdPosition(){
    /*
    
    This function is called every time the main loop runs. When the machine is executing a move it is not called, but when the machine is
    not executing a line it is called regularly and causes the motors to hold their positions.
    
    */
    
    leftAxis.hold();
    rightAxis.hold();
    zAxis.hold();
}