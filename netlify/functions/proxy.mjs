export default async (req) => {
  const corsHeaders = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
    "Access-Control-Allow-Headers": "*",
    "Cross-Origin-Resource-Policy": "cross-origin",
  };

  if (req.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders });
  }

  const url = new URL(req.url);
  const targetUrl = url.searchParams.get("url");

  if (!targetUrl) {
    return new Response("Missing 'url' parameter", { status: 400, headers: corsHeaders });
  }

  try {
    const upstream = await fetch(targetUrl, {
      headers: { "User-Agent": "VESC-Tool-Web/1.0" },
      redirect: "follow",
    });

    const headers = new Headers(upstream.headers);
    Object.entries(corsHeaders).forEach(([k, v]) => headers.set(k, v));

    return new Response(upstream.body, {
      status: upstream.status,
      headers,
    });
  } catch (err) {
    return new Response(`Proxy error: ${err.message}`, { status: 502, headers: corsHeaders });
  }
};

export const config = { path: "/api/proxy" };
