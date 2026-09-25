# Táctil e interacción del usuario

[English](../Touch_and_User_Interaction.md) | [Español](Touch_and_User_Interaction.md)

El panel táctil FT6336G usa la calibración `x=5..475`, `y=5..795` y se sondea cada 50 ms. El táctil informa actividad, despierta light sleep, habilita la ruta activa y puede encender la luz frontal. El movimiento del BMI270 es la otra fuente de actividad y puede despertar mediante M5PM1.

Tocar el panel solicita entrar en controls; si Wi-Fi/API/HA no están listos, la entrada queda pendiente hasta que se recuperen. La zona superior de volver regresa al inicio. GPIO2/GPIO3 navegan por las páginas de controls, sujeto a la limitación actual de múltiples bloques/máximo seis controles descrita en [Habitaciones y controles](Rooms_and_Controls.md). Un clic único del botón POWER también vuelve a Home mientras Controls o Controls pendiente está activo. Se genera un pulso corto de feedback del buzzer para las interacciones compatibles que cambian el estado; el buzzer está en GPIO42 y el pulso dura 25 ms al 8% de salida.

Solo el táctil y el movimiento reinician el tiempo de inactividad. Los botones físicos de página/POWER, las actualizaciones de estado de Home Assistant, las actualizaciones de pantalla, los pulsos NFC/LED de estado y el pulso del buzzer no lo hacen. La vista de controls se cierra antes de light sleep. La actividad del usuario durante la recuperación periódica cancela la recuperación.

La luz frontal normalmente está apagada, se enciende con el brillo configurado en tiempo de ejecución después de la actividad y se apaga cuando vence el timeout de luz frontal medido desde la última actividad táctil/de movimiento. El número de brillo en tiempo de ejecución cambia el valor predeterminado persistente; por sí solo no reaplica el nivel a una luz frontal ya encendida. Controlar directamente la entidad `Frontlight` aplica el nivel solicitado, pero no reinicia el reloj de inactividad táctil/de movimiento. El dispositivo puede permanecer despierto con Wi-Fi/API activa hasta que venza el timeout de sleep independiente.
