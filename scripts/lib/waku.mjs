// Logos Messaging transport — powered by @waku/sdk (Logos Messaging xWaku)
import { createLightNode, waitForRemotePeer, Protocols, utils,
         createEncoder, createDecoder } from "@waku/sdk";

export const LOGOS_NETWORK = { clusterId: 2, numShardsInCluster: 8 };
export const PRESERVE_TOPIC = "/keeper/1/preserve/json";
export const STATUS_TOPIC   = "/keeper/1/status/json";

export async function connect() {
  const node = await createLightNode({ networkConfig: LOGOS_NETWORK, defaultBootstrap: true });
  await node.start();
  await waitForRemotePeer(node, [Protocols.LightPush, Protocols.Filter]);

  const preserveRouting = utils.AutoShardingRoutingInfo.fromContentTopic(PRESERVE_TOPIC, LOGOS_NETWORK);
  const statusRouting   = utils.AutoShardingRoutingInfo.fromContentTopic(STATUS_TOPIC,   LOGOS_NETWORK);

  return {
    node,
    preserveEncoder: createEncoder({ contentTopic: PRESERVE_TOPIC, routingInfo: preserveRouting }),
    statusDecoder:   createDecoder(STATUS_TOPIC, statusRouting),
  };
}

export async function disconnect(node) {
  await node.stop().catch(() => {});
}
