"""Explicit intent adapter following EutherVox DeviceAction's verified shape."""
import uuid

class VoxAdapter:
    def __init__(self, aliases, allowed_users):
        self.aliases = aliases
        self.allowed_users = frozenset(allowed_users)

    def plan(self, transcript, user, target_node):
        if not user or user not in self.allowed_users:
            raise PermissionError('unauthorized EutherVox user')
        alias = ' '.join(transcript.casefold().strip(' .!?').split())
        operation = self.aliases.get(alias)
        if operation is None:
            return None
        device, command = operation
        return {'type': 'action.request', 'action_id': str(uuid.uuid4()),
                'name': 'device.command', 'target': {'kind': 'node', 'node_name': target_node},
                'arguments': {'device': device, 'command': command}, 'requires_confirmation': False}

    @staticmethod
    def spoken(result):
        return {'simulated': 'Kommandot har simulerats. Ingen IR-signal har sänts.',
                'transmitted': 'IR-signalen har sänts. Apparatens tillstånd är inte bekräftat.',
                'accepted': 'Kommandot är mottaget, men sändningen är inte bekräftad.'}.get(result['status'], 'Sändningens resultat är okänt. Jag försöker inte automatiskt igen.')
