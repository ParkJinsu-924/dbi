using System;
using System.Reflection;
using Google.Protobuf;

namespace MMO.Network
{
    // Scans a "handler holder" object for `public event Action<T> OnXxx` declarations
    // (T : IMessage<T>, new()) and registers each one with the given PacketRouter, so
    // that declaring a new event field is the only code needed to bind a new server
    // packet to subscribers.
    //
    // The wrapper installed in the router reads the *current* delegate from the event
    // backing field on every dispatch, so subscribers added/removed at runtime still
    // take effect — same semantics as if the registration had been hand-written.
    //
    // Convention enforced by this scanner:
    //   - Only `Action<T>` fields are considered (C# events compile to private fields
    //     of the delegate type, which we pick up via NonPublic|Instance).
    //   - T must implement IMessage<T> and have a parameterless ctor (the constraint
    //     declared by PacketRouter.Register<T>).
    //   - Anything else on the handler class is ignored — fine to keep helper methods,
    //     state, etc. in the same class.
    //
    // IL2CPP note: registration uses MakeGenericMethod over reference-type T (all
    // protobuf message classes), which IL2CPP handles via generic sharing. Aggressive
    // managed-code-stripping builds should preserve `Proto.*` via link.xml or
    // [UnityEngine.Scripting.Preserve], otherwise the message classes can be removed
    // and reflection will see no event fields to register.
    public static class HandlerAutoRegistrar
    {
        // Cached reflection of PacketRouter.Register<T>(Action<T>) — resolved once.
        private static readonly MethodInfo s_routerRegisterOpen =
            typeof(PacketRouter).GetMethod(nameof(PacketRouter.Register))
            ?? throw new InvalidOperationException(
                "PacketRouter.Register<T>(Action<T>) not found — API renamed?");

        // Cached reflection of MakeInvoker<T> below.
        private static readonly MethodInfo s_makeInvokerOpen =
            typeof(HandlerAutoRegistrar).GetMethod(
                nameof(MakeInvoker),
                BindingFlags.Static | BindingFlags.NonPublic);

        // Walks the handler instance, registers every Action<T> event field, returns
        // the count actually registered (useful for sanity-check logging).
        public static int Register(object handler, PacketRouter router)
        {
            if (handler == null) throw new ArgumentNullException(nameof(handler));
            if (router == null)  throw new ArgumentNullException(nameof(router));

            int count = 0;
            var type = handler.GetType();
            // C# events declared with the simple `event Action<T> OnXxx;` syntax
            // compile to a private backing field of the same name. Hence NonPublic.
            var flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

            foreach (var field in type.GetFields(flags))
            {
                var ft = field.FieldType;
                if (!ft.IsGenericType) continue;
                if (ft.GetGenericTypeDefinition() != typeof(Action<>)) continue;

                var msgType = ft.GetGenericArguments()[0];

                // PacketRouter.Register<T> requires T : IMessage<T>, new().
                // Filter defensively so non-conforming Action<T> fields are skipped
                // rather than throwing at MakeGenericMethod time.
                var imsgT = typeof(IMessage<>).MakeGenericType(msgType);
                if (!imsgT.IsAssignableFrom(msgType)) continue;
                if (msgType.GetConstructor(Type.EmptyTypes) == null) continue;

                // Build a wrapper Action<msgType> that re-reads the field on each call.
                var wrapper = (Delegate)s_makeInvokerOpen
                    .MakeGenericMethod(msgType)
                    .Invoke(null, new object[] { handler, field });

                // router.Register<msgType>(wrapper)
                s_routerRegisterOpen
                    .MakeGenericMethod(msgType)
                    .Invoke(router, new object[] { wrapper });

                count++;
            }
            return count;
        }

        // Generic shim: re-reads the event delegate from the field on every dispatch,
        // so subscribers added/removed AFTER registration still take effect.
        // Per-packet cost: one FieldInfo.GetValue + cast + null-check + invoke —
        // negligible for the packet rates an MMO client deals with.
        private static Action<T> MakeInvoker<T>(object handler, FieldInfo field)
            where T : IMessage<T>, new()
        {
            return msg =>
            {
                var del = (Action<T>)field.GetValue(handler);
                del?.Invoke(msg);
            };
        }
    }
}
