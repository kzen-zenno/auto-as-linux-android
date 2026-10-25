# auto-as-linux

# Test Commands
##  Docker compse build and run.

`docker-compose.yml` lives at the repository root (moved from
`automotive_linux/vehicle_service/docker-compose.yml` so it can also cover
`metapp`; see [`metapp/README.md`](metapp/README.md) §8.2). Run these from
the repository root:

~~~
$ docker compose up -d
$ docker compose ps
# Confirm databroker / gateway-ecu / vehicle-state-provider are Up
~~~
## Open Browser for web can tool at http://localhost:8800/
![image](./docs/images/web%20can%20tool.png)

## Open Browser for met app at http://localhost:6080/vnc.html?autoconnect=trues
![image](./docs/images/metapp.png)

## Get/Set VSS data
~~~
$ docker run --rm --network vehicle_service_kuksa-net fullstorydev/grpcurl:latest   -plaintext   -d '{"entries":[{"path":"Vehicle.Speed","view":"VIEW_CURRENT_VALUE"}]}'   databroker:55555 kuksa.val.v1.VAL/Get
{
  "entries": [
    {
      "path": "Vehicle.Speed",
      "value": {
        "timestamp": "2026-09-06T04:16:05.036193423Z",
        "float": 200
      }
    }
  ]
}
~~~
~~~
$ docker run --rm --network vehicle_service_kuksa-net fullstorydev/grpcurl:latest   -plaintext   -d '{"entries":[{"path":"Vehicle.Powertrain.CombustionEngine.Speed","view":"VIEW_CURRENT_VALUE"}]}'   databrok
er:55555 kuksa.val.v1.VAL/Get
{
  "entries": [
    {
      "path": "Vehicle.Powertrain.CombustionEngine.Speed",
      "value": {
        "timestamp": "2026-09-06T04:17:54.118657776Z",
        "float": 6000
      }
    }
  ]
}
~~~
~~~
$ docker run --rm --network vehicle_service_kuksa-net fullstorydev/grpcurl:latest \
  -plaintext \
  -d '{"updates":[{"entry":{"path":"Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature","actuator_target":{"float":22.5}},"fields":["FIELD_ACTUATOR_TARGET"]}]}' \
  databroker:55555 kuksa.val.v1.VAL/Set
~~~
## Docker compose stop
~~~
$ docker compose down
$ docker compose ps
~~~
