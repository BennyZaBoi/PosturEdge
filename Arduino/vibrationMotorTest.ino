#define MOTOR 18

void setup()
{
  pinMode(MOTOR, OUTPUT);
}

void loop()
{
  digitalWrite(MOTOR, HIGH);
  delay(2000);

  digitalWrite(MOTOR, LOW);
  delay(2000);
}