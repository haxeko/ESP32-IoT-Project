// Chip temperature to MeasuredValue, and when it is reported again (main/c3_temperature.h).
#include "sim.h"

#include <math.h>

#include "c3_temperature.h"

int main(int argc, char **argv)
{
    printf("c3_temperature:\n");

    test_begin("°C to MeasuredValue (0.01 °C), rounded to the nearest");
    CHECK(c3_temperature_to_measured(45.678f) == 4568);
    CHECK(c3_temperature_to_measured(45.674f) == 4567);
    CHECK(c3_temperature_to_measured(20.0f) == 2000);
    CHECK(c3_temperature_to_measured(0.0f) == 0);
    CHECK(c3_temperature_to_measured(-0.004f) == 0);
    CHECK(c3_temperature_to_measured(-12.346f) == -1235);

    test_begin("kept within -40 to 125 °C, a NaN gives the minimum");
    CHECK(c3_temperature_to_measured(125.0f) == kC3TemperatureMax);
    CHECK(c3_temperature_to_measured(300.0f) == kC3TemperatureMax);
    CHECK(c3_temperature_to_measured(-40.0f) == kC3TemperatureMin);
    CHECK(c3_temperature_to_measured(-300.0f) == kC3TemperatureMin);
    CHECK(c3_temperature_to_measured(NAN) == kC3TemperatureMin);

    test_begin("reported first, then on a change of 0.5 °C or more");
    CHECK(c3_temperature_due(false, 0, 4500, 50));
    CHECK(c3_temperature_due(false, 0, 10, 50)); // whatever the stored last value
    CHECK(!c3_temperature_due(true, 4500, 4549, 50));
    CHECK(c3_temperature_due(true, 4500, 4550, 50));
    CHECK(!c3_temperature_due(true, 4500, 4451, 50));
    CHECK(c3_temperature_due(true, 4500, 4450, 50));
    CHECK(c3_temperature_due(true, -100, 100, 50));
    CHECK(c3_temperature_due(true, kC3TemperatureMax, kC3TemperatureMin, 50));

    return test_end();
}
