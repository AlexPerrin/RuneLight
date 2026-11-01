package net.runelite.client.game;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.EnumSet;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Named;
import javax.inject.Singleton;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.events.WorldsFetch;
import net.runelite.client.util.RunnableExceptionLogger;
import net.runelite.http.api.worlds.World;
import net.runelite.http.api.worlds.WorldResult;
import net.runelite.http.api.worlds.WorldType;
import okhttp3.HttpUrl;
import okhttp3.OkHttpClient;

/**
 * RuneLite 1.13.1's WorldService (same fetch: api.runelite.net worlds.js, first run at start, then
 * every 10 minutes while logged in), plus the world the client is on.
 *
 * The api world list holds Jagex's worlds only, so a private world (the test server's 255) isn't in
 * it and plugins that look up {@code findWorld(client.getWorld())} get null (Current World threw an
 * NPE in startUp). getWorlds() therefore adds an entry for the current world when the list lacks it,
 * built from what the client itself knows: id, world flags, and the host and activity of its own
 * world list entry (slr.ws, loaded by the game; see net.runelite.host.CurrentWorld). Nothing extra
 * is fetched. If the api fetch failed, the result holds just the current world (desktop returns null
 * then; a non-null list with the world the player is on is what such plugins need).
 */
@Singleton
public class WorldService
{
	private static final org.slf4j.Logger log = org.slf4j.LoggerFactory.getLogger(WorldService.class);
	private static final int WORLD_FETCH_TIMER = 10; // minutes

	private final Client client;
	private final ScheduledExecutorService scheduledExecutorService;
	private final WorldClient worldClient;
	private final EventBus eventBus;
	private final CompletableFuture<WorldResult> firstRunFuture = new CompletableFuture<>();

	private WorldResult worlds;
	// Set by a failed fetch (NetGuard refuses api.runelite.net off-server): the timer fetches no more, no retry.
	private volatile boolean failed;
	// The last merged result, reused while neither the fetched list nor the current world changed.
	private WorldResult merged;
	private WorldResult mergedFrom;
	private World mergedWorld;

	@Inject
	private WorldService(Client client, ScheduledExecutorService scheduledExecutorService, OkHttpClient okHttpClient,
		@Named("runelite.api.base") HttpUrl apiBase, EventBus eventBus)
	{
		this.client = client;
		this.scheduledExecutorService = scheduledExecutorService;
		this.worldClient = new WorldClient(okHttpClient, apiBase);
		this.eventBus = eventBus;

		scheduledExecutorService.execute(this::tick);
		scheduledExecutorService.scheduleWithFixedDelay(RunnableExceptionLogger.wrap(this::tick),
			(long) (300 + (int) (600 * Math.random())), WORLD_FETCH_TIMER * 60L, TimeUnit.SECONDS);
	}

	private void tick()
	{
		try
		{
			if (!failed && (worlds == null || client.getGameState() == GameState.LOGGED_IN))
			{
				fetch();
			}
		}
		finally
		{
			firstRunFuture.complete(worlds);
		}
	}

	private void fetch()
	{
		log.debug("Fetching worlds");
		try
		{
			WorldResult worldResult = worldClient.lookupWorlds();
			worldResult.getWorlds().sort(Comparator.comparingInt(World::getId));
			worlds = worldResult;
			eventBus.post(new WorldsFetch(worldResult));
		}
		catch (IOException ex)
		{
			failed = true;
			log.warn("Error looking up worlds", ex);
		}
	}

	public void refresh()
	{
		scheduledExecutorService.execute(this::fetch);
	}

	@Nullable
	public WorldResult getWorlds()
	{
		WorldResult fetched;
		if (!firstRunFuture.isDone())
		{
			try
			{
				fetched = firstRunFuture.get(10, TimeUnit.SECONDS);
			}
			catch (InterruptedException | ExecutionException | TimeoutException e)
			{
				log.warn("Failed to retrieve worlds on first run", e);
				fetched = worlds;
			}
		}
		else
		{
			fetched = worlds;
		}
		return withCurrentWorld(fetched);
	}

	private synchronized WorldResult withCurrentWorld(WorldResult fetched)
	{
		int id = client.getWorld();
		if (id <= 0 || (fetched != null && fetched.findWorld(id) != null))
		{
			return fetched;
		}
		World current = currentWorld(id);
		if (merged != null && mergedFrom == fetched && current.equals(mergedWorld))
		{
			return merged;
		}
		List<World> list = new ArrayList<>(fetched != null && fetched.getWorlds() != null ? fetched.getWorlds() : new ArrayList<>());
		list.add(current);
		list.sort(Comparator.comparingInt(World::getId));
		WorldResult result = new WorldResult();
		result.setWorlds(list);
		merged = result;
		mergedFrom = fetched;
		mergedWorld = current;
		return result;
	}

	/** The current world as a world list entry, from the client's own data. */
	private World currentWorld(int id)
	{
		net.runelite.api.World own = null;
		try
		{
			own = net.runelite.host.CurrentWorld.entry(id);
		}
		catch (Throwable t)
		{
			log.debug("world list entry unavailable", t);
		}
		EnumSet<WorldType> types = EnumSet.noneOf(WorldType.class);
		EnumSet<net.runelite.api.WorldType> apiTypes = own != null ? own.getTypes() : client.getWorldType();
		if (apiTypes != null)
		{
			for (net.runelite.api.WorldType t : apiTypes)
			{
				try
				{
					types.add(WorldType.valueOf(t.name()));
				}
				catch (IllegalArgumentException ignored)
				{
					// an api flag with no world list type
				}
			}
		}
		String activity = own != null && own.getActivity() != null && !own.getActivity().isEmpty() ? own.getActivity() : "-";
		return World.builder()
			.id(id)
			.types(types)
			.address(own != null && own.getAddress() != null ? own.getAddress() : "")
			.activity(activity)
			.location(own != null ? own.getLocation() : 0)
			.players(own != null ? own.getPlayerCount() : 0)
			.build();
	}
}
